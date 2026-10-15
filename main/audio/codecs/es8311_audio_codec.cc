#include "es8311_audio_codec.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>

#include "settings.h"

#define TAG "Es8311AudioCodec"
#define DIAG_TAG "AudioDiag"

// Неймспейс NVS для экрана «Настройка эха». Ключи задержки/допуска/флага руки
// здесь же пишет и читает кодек; громкость динамика и усиление микрофона пишет
// экран, а применяет их борд (у кодека на них свой штатный путь через MCP).
#define AEC_TUNE_NS "echo_tune"

Es8311AudioCodec::Es8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port,
                                   int input_sample_rate, int output_sample_rate, gpio_num_t mclk,
                                   gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                                   gpio_num_t pa_pin, uint8_t es8311_addr, bool use_mclk,
                                   bool pa_inverted, bool input_reference) {
    duplex_ = true;  // 是否双工
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    pa_pin_ = pa_pin;
    pa_inverted_ = pa_inverted;
    input_gain_ = 30;

    // Программный опорный канал (эксперимент, см. заголовок): включаем только
    // если плата его запросила И удалось выделить память. При отказе остаёмся
    // в штатном одноканальном режиме — поведение всех остальных ES8311-плат не
    // меняется.
    input_reference_ = false;
    input_channels_ = 1;
    if (input_reference) {
        input_reference_ = StartSoftwareReference();
        if (input_reference_) {
            input_channels_ = 2;
        }
    }
    // Сохранённые с экрана настроек значения (если их нет — остаются
    // расчётные константы). До создания каналов: задержка только наша, внутренняя.
    if (input_reference_) {
        LoadEchoTuning();
    }

    assert(input_sample_rate_ == output_sample_rate_);
    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Output
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = i2c_port,
        .addr = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(ctrl_if_ != NULL);
    ResetCodec();

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = ctrl_if_;
    es8311_cfg.gpio_if = gpio_if_;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = pa_pin;
    es8311_cfg.use_mclk = use_mclk;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    es8311_cfg.pa_reverted = pa_inverted_;
    codec_if_ = es8311_codec_new(&es8311_cfg);

    if (codec_if_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create Es8311AudioCodec");
    } else {
        ESP_LOGI(TAG, "Es8311AudioCodec initialized");
    }
}

void Es8311AudioCodec::ResetCodec() {
    // Hold the ES8311 digital blocks in reset for several milliseconds, as
    // recommended by the initialization guide. Normal codec initialization
    // releases the reset and starts the state machine.
    uint8_t reset_value = 0x1F;
    ESP_ERROR_CHECK(
        static_cast<esp_err_t>(ctrl_if_->write_reg(ctrl_if_, 0x00, 1, &reset_value, 1)));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_LOGI(TAG, "ES8311 software reset complete");
}

Es8311AudioCodec::~Es8311AudioCodec() {
    esp_codec_dev_delete(dev_);

    if (ref_ring_ != nullptr) {
        heap_caps_free(ref_ring_);
        ref_ring_ = nullptr;
    }
    if (mic_ring_ != nullptr) {
        heap_caps_free(mic_ring_);
        mic_ring_ = nullptr;
    }

    audio_codec_delete_codec_if(codec_if_);
    audio_codec_delete_ctrl_if(ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

bool Es8311AudioCodec::StartSoftwareReference() {
    // 32 КБ копии воспроизведения: держим в PSRAM, чтобы не отъедать внутреннюю
    // память у AFE/камеры. Последовательный доступ по 24 кГц для PSRAM тривиален.
    // Размер выбран с запасом под измеренную задержку эха: кольцо должно хранить
    // больше, чем самая длинная проверяемая гипотеза (200 мс) плюс окно
    // корреляции, иначе скан читает уже перезаписанные отсчёты.
    ref_ring_ = static_cast<int16_t*>(
        heap_caps_malloc(kRefRingFrames * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ref_ring_ == nullptr) {
        ref_ring_ = static_cast<int16_t*>(
            heap_caps_malloc(kRefRingFrames * sizeof(int16_t), MALLOC_CAP_INTERNAL |
                             MALLOC_CAP_8BIT));
    }
    if (ref_ring_ == nullptr) {
        ESP_LOGE(TAG, "Не удалось выделить кольцо опорного канала — AEC без reference (моно)");
        return false;
    }
    memset(ref_ring_, 0, kRefRingFrames * sizeof(int16_t));
    // Второе кольцо — сырой микрофон: без него точный скан задержки не сделать,
    // корреляция требует окно в 50 мс с обеих сторон.
    mic_ring_ = static_cast<int16_t*>(
        heap_caps_malloc(kRefRingFrames * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (mic_ring_ == nullptr) {
        mic_ring_ = static_cast<int16_t*>(
            heap_caps_malloc(kRefRingFrames * sizeof(int16_t), MALLOC_CAP_INTERNAL |
                             MALLOC_CAP_8BIT));
    }
    if (mic_ring_ == nullptr) {
        heap_caps_free(ref_ring_);
        ref_ring_ = nullptr;
        ESP_LOGE(TAG, "Не удалось выделить кольцо микрофона — AEC без reference (моно)");
        return false;
    }
    memset(mic_ring_, 0, kRefRingFrames * sizeof(int16_t));
    mic_scratch_.reserve(AUDIO_CODEC_DMA_FRAME_NUM);
    ESP_LOGI(TAG,
             "Программный опорный канал AEC: ВКЛ 2ch [mic,ref], стартовая задержка %d кадров "
             "(~%d мс), кольцо %d кадров",
             kRefDelayFrames, kRefDelayFrames * 1000 / output_sample_rate_, kRefRingFrames);
    return true;
}

void Es8311AudioCodec::UpdateDeviceState() {
    if ((input_enabled_ || output_enabled_) && dev_ == nullptr) {
        esp_codec_dev_cfg_t dev_cfg = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
            .codec_if = codec_if_,
            .data_if = data_if_,
        };
        dev_ = esp_codec_dev_new(&dev_cfg);
        assert(dev_ != NULL);

        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)input_sample_rate_,
            .mclk_multiple = 0,
        };
        esp_err_t err = esp_codec_dev_open(dev_, &fs);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ES8311: esp_codec_dev_open failed: 0x%x", err);
            ESP_LOGE(TAG,
                     "ES8311: не аварийно завершаем приложение — оставляем камеру/CSI доступными");
            esp_codec_dev_delete(dev_);
            dev_ = nullptr;
            if (pa_pin_ != GPIO_NUM_NC) {
                gpio_set_level(pa_pin_, pa_inverted_ ? 1 : 0);
            }
            LogDiagnostics();
            return;
        }

        err = esp_codec_dev_set_in_gain(dev_, input_gain_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ES8311: set input gain failed: 0x%x", err);
        }

        err = esp_codec_dev_set_out_vol(dev_, output_volume_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ES8311: set output volume failed: 0x%x", err);
        }

        // Однократный дамп после первого открытия: здесь регистры отражают
        // РЕАЛЬНОЕ рабочее состояние (ADC/DAC/PA), а не сброс.
        if (!diag_logged_) {
            diag_logged_ = true;
            LogDiagnostics();
        }
    } else if (!input_enabled_ && !output_enabled_ && dev_ != nullptr) {
        ESP_ERROR_CHECK(esp_codec_dev_close(dev_));
        esp_codec_dev_delete(dev_);
        dev_ = nullptr;
    }
    if (pa_pin_ != GPIO_NUM_NC) {
        int level = output_enabled_ ? 1 : 0;
        gpio_set_level(pa_pin_, pa_inverted_ ? !level : level);
    }
}

void Es8311AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                                            gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg =
            {
                .sample_rate_hz = (uint32_t)output_sample_rate_,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
                .ext_clk_freq_hz = 0,
#endif
            },
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO,
                     .slot_mask = I2S_STD_SLOT_BOTH,
                     .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .ws_pol = false,
                     .bit_shift = true,
#ifdef I2S_HW_VERSION_2
                     .left_align = true,
                     .big_endian = false,
                     .bit_order_lsb = false
#endif
        },
        .gpio_cfg = {.mclk = mclk,
                     .bclk = bclk,
                     .ws = ws,
                     .dout = dout,
                     .din = din,
                     .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    ESP_LOGI(TAG, "Duplex channels created");
}

void Es8311AudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (dev_ != nullptr) {
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(dev_, volume));
    }
    AudioCodec::SetOutputVolume(volume);
}

void Es8311AudioCodec::SetInputGain(float gain) {
    // ES8311 умеет 0..42 дБ (шаг 6 дБ). Значение вне диапазона — это не
    // «громче/тише», а ошибка ctrl_if: прошивке кажется, что усиление
    // применено, а регистр остаётся прежним. Старый экран LVGL сохранял
    // проценты (0..100), поэтому в NVS мог лежать «72 дБ» — режем на входе,
    // чтобы хотя бы первый после загрузки прошивки вызов был валидным.
    if (gain < 0.0f || gain > 42.0f) {
        ESP_LOGW(TAG, "Усиление микрофона %.1f дБ вне диапазона 0..42, беру %.1f дБ", gain,
                 gain < 0.0f ? 0.0f : 42.0f);
        gain = std::clamp(gain, 0.0f, 42.0f);
    }

    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (dev_ != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_in_gain(dev_, gain));
    }
    AudioCodec::SetInputGain(gain);
}

void Es8311AudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (codec_if_ == nullptr) {
        return;
    }
    if (enable == input_enabled_) {
        return;
    }
    AudioCodec::EnableInput(enable);
    UpdateDeviceState();
}

void Es8311AudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (codec_if_ == nullptr) {
        return;
    }
    if (enable == output_enabled_) {
        return;
    }
    AudioCodec::EnableOutput(enable);
    UpdateDeviceState();
}

int Es8311AudioCodec::Read(int16_t* dest, int samples) {
    if (!input_reference_) {
        if (input_enabled_) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_codec_dev_read(dev_, (void*)dest, samples * sizeof(int16_t)));
        }
        return samples;
    }

    // samples — это общее число int16 (AudioCodec::InputData передаёт data.size()),
    // т.е. кадров input_channels_ = 2. Аппаратно АЦП моно, поэтому читаем
    // ровно половину, а второй канал берём из копии воспроизведения.
    const int frames = samples / input_channels_;
    if (static_cast<int>(mic_scratch_.size()) < frames) {
        mic_scratch_.resize(frames);
    }
    int16_t* mic = mic_scratch_.data();
    if (input_enabled_ && dev_ != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_codec_dev_read(dev_, (void*)mic, frames * sizeof(int16_t)));
    } else {
        memset(mic, 0, frames * sizeof(int16_t));
    }

    std::lock_guard<std::mutex> lock(ref_mutex_);
    for (int i = 0; i < frames; ++i) {
        dest[i * 2] = mic[i];
        dest[i * 2 + 1] = TakeReferenceLocked();
    }
    TrackEchoAlignment(mic, frames);
    return samples;
}

int16_t Es8311AudioCodec::TakeReferenceLocked() {
    const int64_t available = ref_write_pos_ - ref_read_pos_;
    if (available < ref_delay_frames_) {
        if (available < 0) {
            // Указатель чтения ушёл вперёд (например, после остановки
            // воспроизведения) — догоняем запись и молчим.
            ref_read_pos_ = ref_write_pos_;
        }
        return 0;
    }
    if (available > ref_delay_frames_ + ref_tolerance_frames_) {
        // Длинная пауза в воспроизведении: хвост устарел, перезахватываем
        // позицию чтения, чтобы опорный сигнал не «плыл» на весь буфер.
        ref_read_pos_ = ref_write_pos_ - ref_delay_frames_;
    }
    return ref_ring_[ref_read_pos_++ & kRefRingMask];
}

void Es8311AudioCodec::TrackEchoAlignment(const int16_t* mic, int frames) {
    if (lag_locked_) {
        return;  // задержка измерена, больше считать нечего
    }
    // Кольцо микрофона — материал для точного скана; пишем его всегда, пока
    // оценка не зафиксирована.
    if (mic_ring_ != nullptr) {
        for (int i = 0; i < frames; ++i) {
            mic_ring_[(mic_write_pos_ + i) & kRefRingMask] = mic[i];
        }
        mic_write_pos_ += frames;
    }

    const int64_t head = ref_write_pos_;
    if (lag_window_frames_ == 0) {
        lag_ref_write_start_ = head;  // отсчёт «сколько записано в этом окне»
    }
    for (int i = 0; i < frames; ++i) {
        const int64_t m = mic[i];
        const int64_t r = ref_ring_[(head - frames + i) & kRefRingMask];
        lag_mic_energy_ += m * m;
        lag_ref_energy_ += r * r;
    }
    lag_window_frames_ += frames;

    if (!output_enabled_ || head == 0) {
        run_start_ref_head_ = head;
        last_ref_head_ = head;
        stall_chunks_ = 0;
        return;
    }
    // Кадры опоры — это таймлайн ЦАПа: ref_write_pos_ двигается только когда
    // звук пишут в DMA, а микрофон считается всегда. Значит «лаги имеют смысл»
    // лишь внутри непрерывного прогона воспроизведения, и его начало мы храним
    // в run_start_ref_head_. Пропуск записи на один-два чанка чтения — это
    // джиттер комиттера вывода, а не пауза в звуке, поэтому прогон обрывается
    // только после kFineStallToleranceChunks пустых чанков подряд.
    const int64_t ref_delta = head - last_ref_head_;
    last_ref_head_ = head;
    if (ref_delta > 0) {
        stall_chunks_ = 0;
    } else if (++stall_chunks_ > kFineStallToleranceChunks) {
        stall_chunks_ = 0;
        run_start_ref_head_ = head;
    }

    const int64_t run_frames = head - run_start_ref_head_;
    // Сколько гипотез успеем проверить при этом прогоне: окно W + лаг L не
    // должны вылезать за начало прогона и за кольцо.
    const int max_lag = MaxScanLagLocked();
    if (max_lag >= kFineLagStepFrames * 4 &&
        mic_write_pos_ - last_scan_at_ >= kFineScanCooldownFrames) {
        ScanEchoDelayFineLocked(max_lag);
    }
    if (lag_window_frames_ >= kLagWindowFrames) {
        ReportWindowLocked(run_frames, max_lag);
    }
}

int Es8311AudioCodec::MaxScanLagLocked() const {
    const int64_t run = ref_write_pos_ - run_start_ref_head_;
    int64_t lag = run - kFineWindowFrames;
    if (lag > kFineMaxLagFrames) {
        lag = kFineMaxLagFrames;
    }
    if (lag > kRefRingFrames - kFineWindowFrames) {
        lag = kRefRingFrames - kFineWindowFrames;
    }
    return lag > 0 ? static_cast<int>(lag) : 0;
}

void Es8311AudioCodec::ReportWindowLocked(int64_t run_frames, int max_lag) {
    if (lag_window_frames_ == 0) {
        return;
    }
    // Числа окна говорят, что именно искать: если опора пишется, а корреляции
    // нет — дело в задержке/интерливинге; если не пишется — кольцо пустое.
    const double mic_rms = std::sqrt(static_cast<double>(lag_mic_energy_) / lag_window_frames_);
    const double ref_rms = std::sqrt(static_cast<double>(lag_ref_energy_) / lag_window_frames_);
    ESP_LOGI(DIAG_TAG,
             "AEC-окно: кадров %lld, опоры записано %lld, RMS мик=%.0f, RMS опора=%.0f, "
             "задержка=%d кадров, прогон=%lld кадров, скан до %d кадров (~%d мс)",
             (long long)lag_window_frames_, (long long)(ref_write_pos_ - lag_ref_write_start_),
             mic_rms, ref_rms, ref_delay_frames_, (long long)run_frames, max_lag,
             max_lag * 1000 / output_sample_rate_);
    lag_mic_energy_ = 0;
    lag_ref_energy_ = 0;
    lag_window_frames_ = 0;
}

void Es8311AudioCodec::ScanEchoDelayFineLocked(int max_lag_frames) {
    if (lag_locked_) {
        return;  // задержка уже измерена и зафиксирована, кольцо микрофона снято
    }
    const int64_t head = ref_write_pos_;
    const int64_t run_frames = head - run_start_ref_head_;
    const int64_t need = kFineWindowFrames + max_lag_frames;
    if (head < need || mic_write_pos_ < kFineWindowFrames || run_frames < need ||
        mic_ring_ == nullptr) {
        ESP_LOGI(DIAG_TAG,
                 "AEC-скан: пропущен (прогон %lld кадров из %lld нужных, лаг до %d, "
                 "микрофон %lld кадров)",
                 (long long)run_frames, (long long)need, max_lag_frames,
                 (long long)mic_write_pos_);
        return;
    }
    last_scan_at_ = mic_write_pos_;

    const int64_t mic_start = mic_write_pos_ - kFineWindowFrames;
    // Средние вычитаем: и в микрофоне, и в опоре есть постоянная составляющая
    // и шум квантования, без нормировки по центру «корреляция» получается
    // положительно почти на всех гипотезах и пик не виден.
    float m_mean = 0.f;
    for (int i = 0; i < kFineWindowFrames; ++i) {
        m_mean += mic_ring_[(mic_start + i) & kRefRingMask];
    }
    m_mean /= kFineWindowFrames;

    float m_energy = 0.f;
    for (int i = 0; i < kFineWindowFrames; ++i) {
        const float m = mic_ring_[(mic_start + i) & kRefRingMask] - m_mean;
        m_energy += m * m;
    }
    if (m_energy < 1.f) {
        return;  // микрофон молчит — коррелировать нечего
    }

    int best_lag = -1;
    int second_lag = -1;
    float best_rho = 0.f;
    float second_rho = 0.f;
    for (int lag = 0; lag <= max_lag_frames; lag += kFineLagStepFrames) {
        const int64_t ref_base = head - kFineWindowFrames - lag;
        float r_mean = 0.f;
        for (int i = 0; i < kFineWindowFrames; ++i) {
            r_mean += ref_ring_[(ref_base + i) & kRefRingMask];
        }
        r_mean /= kFineWindowFrames;
        float corr = 0.f;
        float r_energy = 0.f;
        for (int i = 0; i < kFineWindowFrames; ++i) {
            const float m = mic_ring_[(mic_start + i) & kRefRingMask] - m_mean;
            const float r = ref_ring_[(ref_base + i) & kRefRingMask] - r_mean;
            corr += m * r;
            r_energy += r * r;
        }
        if (r_energy < 1.f) {
            continue;  // на этой гипотезе опора глухая (тишина в прогоне)
        }
        const float rho = corr / std::sqrt(m_energy * r_energy);
        if (rho > best_rho) {
            second_lag = best_lag;
            second_rho = best_rho;
            best_lag = lag;
            best_rho = rho;
        } else if (rho > second_rho) {
            second_lag = lag;
            second_rho = rho;
        }
    }

    const int lag_ms = best_lag >= 0 ? best_lag * 1000 / output_sample_rate_ : -1;
    const int second_ms = second_lag >= 0 ? second_lag * 1000 / output_sample_rate_ : -1;
    ESP_LOGI(DIAG_TAG,
             "AEC-скан: окно %d кадров, прогон %lld, гипотезы до %d мс, лучший лаг %d мс "
             "(r=%.3f), второй %d мс (r=%.3f), текущая задержка %d кадров (~%d мс)",
             kFineWindowFrames, (long long)run_frames, max_lag_frames * 1000 / output_sample_rate_,
             lag_ms, best_rho, second_ms, second_rho, ref_delay_frames_,
             ref_delay_frames_ * 1000 / output_sample_rate_);

    if (best_rho >= 0.30f && best_lag > kFineLagStepFrames && best_lag < max_lag_frames) {
        // Пик уверенный и не на границе поиска — фиксируем измеренную задержку
        // и выключаем оценку.
        ref_delay_frames_ = best_lag > kRefRingFrames - kFineWindowFrames
                                ? kRefRingFrames - kFineWindowFrames
                                : best_lag;
        // Чтение обязано сразу переехать на новую задержку, иначе следующий
        // буфер отдаст в AEC устаревший хвост кольца.
        ref_read_pos_ = ref_write_pos_ - ref_delay_frames_;
        lag_locked_ = true;
        measured_delay_frames_ = ref_delay_frames_;
        manual_delay_ = false;
        // Кольцо сырого микрофона нужно было только для измерения — освобождаем
        // его 32 КБ PSRAM; дальше TrackEchoAlignment выходит по флагу сразу.
        heap_caps_free(mic_ring_);
        mic_ring_ = nullptr;
        ESP_LOGI(DIAG_TAG,
                 "AEC-скан: задержка эха %d кадров (~%d мс) зафиксирована, поиск выключен",
                 ref_delay_frames_, ref_delay_frames_ * 1000 / output_sample_rate_);
    } else if (best_rho >= 0.30f && best_lag == max_lag_frames) {
        // Первый замер на железе дал ровно такой случай: прогон 4320 кадров
        // ограничил диапазон 130 мс, и «пик» вышел на границу — это не
        // измерение, а обрезок. Ждём более длинного непрерывного звука.
        ESP_LOGW(DIAG_TAG,
                 "AEC-скан: лучший лаг %d мс совпал с границей поиска — настоящая задержка "
                 "может быть больше, нужен более длинный прогон",
                 lag_ms);
    }
}

void Es8311AudioCodec::LogEchoAlignment() {
    std::lock_guard<std::mutex> lock(ref_mutex_);
    if (!input_reference_) {
        ESP_LOGI(DIAG_TAG, "AEC-согласование: программный опорный канал выключен");
        return;
    }
    ReportWindowLocked(ref_write_pos_ - run_start_ref_head_, MaxScanLagLocked());
    ScanEchoDelayFineLocked(MaxScanLagLocked());
}

int Es8311AudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_codec_dev_write(dev_, (void*)data, samples * sizeof(int16_t)));
        if (input_reference_) {
            // Копия уходящего в DMA звука для опорного канала AEC. Кольцо
            // циклическое, поэтому здесь нет memmove, как в box_audio_codec_lite.
            std::lock_guard<std::mutex> lock(ref_mutex_);
            for (int i = 0; i < samples; ++i) {
                ref_ring_[(ref_write_pos_ + i) & kRefRingMask] = data[i];
            }
            ref_write_pos_ += samples;
        }
    }
    return samples;
}

// Пересчёт кадров в миллисекунды и обратно. Основа одна: и микрофон, и ЦАП
// сидят на одном WS/BCLK (full-duplex, 24 кГц), поэтому опорное кольцо живёт на
// той же временной шкале, что и приём. Экран настроек и NVS оперуют
// миллисекундами — так значения понятны человеку.
namespace {
inline int FramesToMs(int frames, int rate) {
    return rate > 0 ? frames * 1000 / rate : 0;
}
inline int MsToFrames(int ms, int rate) {
    return rate > 0 ? ms * rate / 1000 : 0;
}
inline int ClampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

int Es8311AudioCodec::EchoDelayMs() {
    if (!input_reference_) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(ref_mutex_);
    return FramesToMs(ref_delay_frames_, output_sample_rate_);
}

void Es8311AudioCodec::SetEchoDelayMs(int ms) {
    if (!input_reference_) {
        return;
    }
    std::lock_guard<std::mutex> lock(ref_mutex_);
    // Нижняя граница — физика: очередь DMA-описателей (6 x 240 = 1440 кадров,
    // 60 мс) такова, что отсчёт не может зазвучать раньше. Верхняя — размер
    // кольца минус окно корреляции, иначе TakeReferenceLocked читает
    // перезаписанные отсчёты.
    const int max_frames = kRefRingFrames - kFineWindowFrames;
    int frames = ClampInt(MsToFrames(ms, output_sample_rate_), 0, max_frames);
    ref_delay_frames_ = frames;
    // Ручное значение старше автопоиска: иначе коррелятор на следующем громком
    // слове перешибёт настройку пользователя.
    lag_locked_ = true;
    manual_delay_ = true;
    measured_delay_frames_ = frames;
    // Чтение обязано переехать на новую задержку немедленно, иначе первый же
    // буфер отдаст в AEC устаревший хвост кольца.
    ref_read_pos_ = ref_write_pos_ - frames;
    ESP_LOGI(DIAG_TAG, "AEC-настройка: задержка эха выставлена вручную: %d мс (%d кадров)",
             FramesToMs(frames, output_sample_rate_), frames);
}

int Es8311AudioCodec::RefToleranceMs() {
    if (!input_reference_) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(ref_mutex_);
    return FramesToMs(ref_tolerance_frames_, output_sample_rate_);
}

void Es8311AudioCodec::SetRefToleranceMs(int ms) {
    if (!input_reference_) {
        return;
    }
    std::lock_guard<std::mutex> lock(ref_mutex_);
    // 5..150 мс: меньше 5 мс — опорный канал «дёргается» на каждом джиттере
    // комиттера вывода, больше 150 мс — в AEC едет рассогласование.
    ref_tolerance_frames_ =
        ClampInt(MsToFrames(ms, output_sample_rate_), MsToFrames(5, output_sample_rate_),
                 MsToFrames(150, output_sample_rate_));
    ESP_LOGI(DIAG_TAG, "AEC-настройка: допуск рассинхрона %d мс (%d кадров)",
             FramesToMs(ref_tolerance_frames_, output_sample_rate_), ref_tolerance_frames_);
}

void Es8311AudioCodec::RestartEchoScan() {
    if (!input_reference_) {
        return;
    }
    std::lock_guard<std::mutex> lock(ref_mutex_);
    // Кольцо сырого микрофона коррелятор освобождает сразу после фиксации пика
    // (экономия 32 КБ PSRAM) — при повторном запуске выделяем его заново.
    if (mic_ring_ == nullptr) {
        mic_ring_ = static_cast<int16_t*>(heap_caps_malloc(kRefRingFrames * sizeof(int16_t),
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (mic_ring_ == nullptr) {
            mic_ring_ = static_cast<int16_t*>(heap_caps_malloc(
                kRefRingFrames * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (mic_ring_ == nullptr) {
            ESP_LOGE(DIAG_TAG, "AEC-скан: нет памяти для кольца микрофона — поиск не запустить");
            return;
        }
        memset(mic_ring_, 0, kRefRingFrames * sizeof(int16_t));
    }
    lag_locked_ = false;
    manual_delay_ = false;
    measured_delay_frames_ = 0;
    // Сбрасываем и прогон, и окно, и счётчики — иначе скан стартует с
    // «грязными» энергиями и может выдать пик на склейке двух сессий.
    mic_write_pos_ = 0;
    run_start_ref_head_ = ref_write_pos_;
    last_ref_head_ = ref_write_pos_;
    stall_chunks_ = 0;
    lag_mic_energy_ = 0;
    lag_ref_energy_ = 0;
    lag_window_frames_ = 0;
    lag_ref_write_start_ = ref_write_pos_;
    // Отрицательная «последняя проверка» = кулдаун уже истёк, скан возможен
    // на первом же подходящем чанке.
    last_scan_at_ = -kFineScanCooldownFrames;
    ESP_LOGI(DIAG_TAG, "AEC-настройка: автопоиск задержки эха перезапущен");
}

bool Es8311AudioCodec::EchoDelayMeasured(int* ms) {
    std::lock_guard<std::mutex> lock(ref_mutex_);
    if (measured_delay_frames_ <= 0) {
        return false;
    }
    if (ms != nullptr) {
        *ms = FramesToMs(measured_delay_frames_, output_sample_rate_);
    }
    return true;
}

bool Es8311AudioCodec::EchoDelayManual() {
    std::lock_guard<std::mutex> lock(ref_mutex_);
    return manual_delay_;
}

void Es8311AudioCodec::LoadEchoTuning() {
    Settings settings(AEC_TUNE_NS, false);
    const int echo_ms = settings.GetInt("echo_ms", 0);
    const int tol_ms = settings.GetInt("tol_ms", 0);
    const bool manual = settings.GetBool("manual", false);
    // Чувствительность микрофона у AudioCodec штатного сохранения не имеет
    // (в отличие от громкости динамика), поэтому держим её в этом же неймспейсе.
    const int in_gain = settings.GetInt("in_gain", -1);
    // Логируем всё, что реально лежит в NVS: только так по логу видно, что
    // значения с экрана настроек пережили перезагрузку (когда ключей нет,
    // ниже не печатается вообще ничего, и «пустой лог» невозможно отличить от
    // «значения есть, но они нулевые»).
    ESP_LOGI(DIAG_TAG,
             "AEC-настройка: в памяти устройства — задержка %d мс, допуск %d мс, микрофон %d дБ, %s",
             echo_ms, tol_ms, in_gain, manual ? "вручную" : "авто");
    if (in_gain >= 0) {
        // В NVS может лежать значение со старых прошивок, где этот ползунок
        // был в процентах (0..100). 72 «дБ» для ES8311 — ошибка записи
        // регистра, поэтому при загрузке сразу приводим к реальному
        // диапазону усиления.
        const int clamped_gain = std::clamp(in_gain, 0, 42);
        if (clamped_gain != in_gain) {
            ESP_LOGW(TAG, "Усиление микрофона из памяти %d дБ вне диапазона 0..42, применяю %d дБ",
                     in_gain, clamped_gain);
        }
        input_gain_ = clamped_gain;
        saved_input_gain_ = true;
    }
    if (tol_ms > 0) {
        ref_tolerance_frames_ = ClampInt(MsToFrames(tol_ms, output_sample_rate_),
                                        MsToFrames(5, output_sample_rate_),
                                        MsToFrames(150, output_sample_rate_));
    }
    if (manual && echo_ms > 0) {
        // Ручная настройка с прошлого включения: задержку берём из NVS и
        // автопоиск сразу оставляем выключенным.
        {
            std::lock_guard<std::mutex> lock(ref_mutex_);
            ref_delay_frames_ =
                ClampInt(MsToFrames(echo_ms, output_sample_rate_), 0,
                         kRefRingFrames - kFineWindowFrames);
            lag_locked_ = true;
            manual_delay_ = true;
            measured_delay_frames_ = ref_delay_frames_;
        }
        ESP_LOGI(DIAG_TAG, "AEC-настройка: загружены ручные значения — задержка %d мс, допуск %d мс",
                 echo_ms, FramesToMs(ref_tolerance_frames_, output_sample_rate_));
    } else if (echo_ms > 0) {
        // Значение есть, но оно было измерено автоматически: используем как
        // стартовую догадку, коррелятор при желании уточнит его сам.
        ref_delay_frames_ =
            ClampInt(MsToFrames(echo_ms, output_sample_rate_), 0, kRefRingFrames - kFineWindowFrames);
        ESP_LOGI(DIAG_TAG, "AEC-настройка: загружено измеренное значение %d мс (поиск активен)",
                 echo_ms);
    }
}

void Es8311AudioCodec::SaveEchoTuning() {
    if (!input_reference_) {
        return;
    }
    int echo_ms = 0;
    int tol_ms = 0;
    bool manual = false;
    int gain = 0;
    {
        std::lock_guard<std::mutex> lock(ref_mutex_);
        echo_ms = FramesToMs(ref_delay_frames_, output_sample_rate_);
        tol_ms = FramesToMs(ref_tolerance_frames_, output_sample_rate_);
        manual = manual_delay_;
        gain = static_cast<int>(input_gain_);
    }
    // Мьютекс опорного канала отпущен выше: NVS пишет во flash, а Read() ждёт
    // этот же мьютекс на каждом чанке — держать его во время записи означало бы
    // щелчки в звуке.
    Settings settings(AEC_TUNE_NS, true);
    settings.SetInt("echo_ms", echo_ms);
    settings.SetInt("tol_ms", tol_ms);
    settings.SetInt("in_gain", gain);
    settings.SetBool("manual", manual);
    ESP_LOGI(DIAG_TAG,
             "AEC-настройка: сохранены значения — задержка %d мс, допуск %d мс, микрофон %d дБ, %s",
             echo_ms, tol_ms, gain, manual ? "вручную" : "авто");
}

void Es8311AudioCodec::LogDiagnostics() {
    // Чтение регистра через control-интерфейс esp_codec_dev. Сигнатура
    // (self, reg, reg_len, data, data_len) — та же, что у write_reg в
    // ResetCodec(). Возврат 0 = ESP_OK. Никаких abort: диагностика должна
    // работать даже на «полумёртвой» шине.
    auto read_reg = [this](int reg, uint8_t* val) -> bool {
        return ctrl_if_ != nullptr && ctrl_if_->read_reg(ctrl_if_, reg, 1, val, 1) == 0;
    };

    ESP_LOGI(DIAG_TAG, "=== Состояние кодека ===");
    ESP_LOGI(DIAG_TAG, "ввод=%d вывод=%d громкость=%d усиление_вх=%d dev_open=%s",
             (int)input_enabled_, (int)output_enabled_, output_volume_, (int)input_gain_,
             dev_ != nullptr ? "да" : "нет");
    if (input_reference_) {
        ESP_LOGI(DIAG_TAG,
                 "опорный канал AEC: вкл (2ch [mic,ref]), запись=%lld чтение=%lld "
                 "перекос=%lld кадров",
                 (long long)ref_write_pos_, (long long)ref_read_pos_,
                 (long long)(ref_write_pos_ - ref_read_pos_));
    } else {
        ESP_LOGI(DIAG_TAG, "опорный канал AEC: выкл (моно вход)");
    }
    if (pa_pin_ != GPIO_NUM_NC) {
        int level = gpio_get_level(pa_pin_);
        ESP_LOGI(DIAG_TAG, "PA: GPIO%d уровень=%d (активный %s)", (int)pa_pin_, level,
                 pa_inverted_ ? "LOW" : "HIGH");
        if (output_enabled_ && !pa_inverted_ && level == 0) {
            ESP_LOGW(DIAG_TAG, "вывод включён, но PA=0 — усилитель ВЫКЛЮЧЕН (пин/полярность?)");
        }
    }
    if (codec_if_ == nullptr) {
        ESP_LOGE(DIAG_TAG, "codec_if не создан — ES8311 не инициализирован");
        return;
    }

    // Полный дамп 0x00..0x49 (аналог es8311_register_dump из esp-bsp,
    // но через ctrl_if). 8 регистров в строке; '--' = ошибка чтения.
    ESP_LOGI(DIAG_TAG, "ES8311 регистры 0x00..0x49:");
    for (int base = 0x00; base < 0x4A; base += 8) {
        char line[48];
        int n = snprintf(line, sizeof(line), "  ");
        for (int r = base; r < base + 8 && r < 0x4A; ++r) {
            uint8_t v = 0;
            if (read_reg(r, &v)) {
                n += snprintf(line + n, sizeof(line) - n, "%02x ", v);
            } else {
                n += snprintf(line + n, sizeof(line) - n, "-- ");
            }
        }
        ESP_LOGI(DIAG_TAG, "0x%02X:%s", base, line);
    }

    // Ключевые регистры с ориентирами штатной инициализации esp_codec_dev
    // (могут отличаться по битам clock-конфига; смысл — заметить ГРУБОЕ
    // отклонение: мьют, выключенный блок, нулевая громкость).
    static const struct {
        int reg;
        const char* desc;
    } keys[] = {
        {0x0D, "аналоговые HP-драйверы (ориентир 0x01)"},
        {0x0E, "PGA/модулятор АЦП — микрофон (ориентир 0x02)"},
        {0x12, "ЦАП (ориентир 0x00 = включён)"},
        {0x13, "выходной каскад HP (ориентир 0x10)"},
        {0x17, "усиление АЦП (старший ниббл, шаг PGA)"},
        {0x31, "мьют ЦАП (биты 5..6 = 0x60 — замьючен!)"},
        {0x32, "громкость ЦАП (0x00 = тишина, растёт с громкостью)"},
    };
    ESP_LOGI(DIAG_TAG, "=== Ключевые регистры ===");
    for (const auto& k : keys) {
        uint8_t v = 0;
        if (read_reg(k.reg, &v)) {
            ESP_LOGI(DIAG_TAG, "REG 0x%02X = 0x%02X  %s", k.reg, v, k.desc);
        } else {
            ESP_LOGE(DIAG_TAG, "REG 0x%02X = НЕ ЧИТАЕТСЯ  %s", k.reg, k.desc);
        }
    }
    uint8_t mute_reg = 0;
    if (read_reg(0x31, &mute_reg) && (mute_reg & 0x60)) {
        ESP_LOGW(DIAG_TAG,
                 "ЦАП замьючен программно (0x31=0x%02X, биты 0x60): громкость или mute-логика",
                 mute_reg);
    }
}
