#ifndef _ES8311_AUDIO_CODEC_H
#define _ES8311_AUDIO_CODEC_H

#include "audio_codec.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>
#include <vector>

class Es8311AudioCodec : public AudioCodec {
private:
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if_ = nullptr;
    const audio_codec_if_t* codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;

    esp_codec_dev_handle_t dev_ = nullptr;
    gpio_num_t pa_pin_ = GPIO_NUM_NC;
    bool pa_inverted_ = false;
    bool diag_logged_ = false;  // однократный дамп после первого открытия кодека
    std::mutex data_if_mutex_;

    // ---- ЭКСПЕРИМЕНТ: программный опорный канал для AEC -----------------
    // ES8311 — одноканальный АЦП с единственной линией данных (ASDOUT),
    // поэтому аппаратный «возврат» звука динамика отдельным каналом
    // подключить нельзя. Write() кладёт копию уходящих в DMA отсчётов в
    // кольцевой буфер, а Read() отдаёт их ВТОРЫМ каналом в интерливинге
    // [mic, ref]. Выше по конвейеру это штатный путь: AudioService
    // ресемплит две дорожки (audio_service.cc:74-78), afe_audio_engine
    // сам строит формат входа "MR" и включает aec по input_reference()
    // (afe_audio_engine.cc:132-150).
    static constexpr int kRefRingFrames = 16384;  // степень 2, ~680 мс при 24 кГц
    static constexpr int kRefRingMask = kRefRingFrames - 1;
    // Первоначальная догадка о задержке опорного сигнала. Мик и ДАЦ работают на
    // одном WS/BCLK (full-duplex, один порт), поэтому отсчёт, записанный в
    // Write(), звучит из динамика спустя очередь DMA-описателей
    // (AUDIO_CODEC_DMA_DESC_NUM x AUDIO_CODEC_DMA_FRAME_NUM = 6 x 240 = 1440
    // кадров), а в Read() приходит ещё через буфер приёма + акустический путь.
    // На глаз это ~70 мс. На реальной плате (JC1060P470C, 24 кГц) коррелятор
    // ниже измерил 1824 кадра (~76 мс, r=0.78) — это и есть стартовое значение,
    // а оценка на ходу остаётся на случай, если железо/громкость/тракт дадут
    // другой перекос: измеренный пик подставляется в ref_delay_frames_ один раз
    // и поиск выключается.
    static constexpr int kRefDelayFrames = 1824;         // ~76 мс при 24 кГц, измерено
    static constexpr int kRefToleranceFrames = 1200;     // допуск перекоса, ~50 мс

    int16_t* ref_ring_ = nullptr;  // PSRAM-кольцо копии воспроизведения
    int16_t* mic_ring_ = nullptr;   // PSRAM-кольцо сырого микрофона (для точного
                                    // поиска задержки по тонкой сетке)
    int64_t ref_write_pos_ = 0;    // сколько кадров опорного сигнала записано
    int64_t mic_write_pos_ = 0;    // сколько кадров микрофона записано
    int64_t ref_read_pos_ = 0;     // следующий кадр, выдаваемый в Read()
    int ref_delay_frames_ = kRefDelayFrames;  // активная задержка, корректируется
    // Те же две величины, но уже как рабочие (ручные) параметры: допуск
    // перекоса экран настроек крутит так же, как и задержку.
    int ref_tolerance_frames_ = kRefToleranceFrames;
    int measured_delay_frames_ = 0;  // что замерил коррелятор (0 — ещё не мерил)
    bool manual_delay_ = false;      // задержка выставлена руками, автопоиск снят
    std::vector<int16_t> mic_scratch_;
    std::mutex ref_mutex_;

    // Оценка фактической задержки эха. Первоначальный потоковый коррелятор по
    // сетке гипотез 10..300 мс на железе не нашёл пик вообще (r<=0.08 при
    // заметно работающих и динамике, и микрофоне): позиция записи кольца
    // двигается только во время воспроизведения, а позиция микрофона — всегда,
    // поэтому в 10-секундном окне, где речь занимает треть времени, гипотезы
    // лага «плывут» и пик размывается в шум. Скан делаем точечно: по последним
    // 50 мс сигнала на сетке с шагом 2 мс. Всё во float — у P4 аппаратный
    // FPU только для одинарной точности, double считался бы эмуляцией.
    //
    // Диапазон поиска ограничен сверху длиной непрерывного воспроизведения:
    // гипотеза лага L читает опору в [head-W-L, head-L), и эти кадры должны
    // относиться к ТЕКУЩЕЙ сессии звука, иначе коррелируем с прошлой фразой.
    // Поэтому требуем непрерывного прогона не прежние 500 мс (на железе столько
    // сплошного звука не набиралось — оценка никогда не срабатывала), а
    // столько, сколько реально есть: максимальный лаг считаем от длины прогона.
    // Прогон обрывается только паузой дольше kFineStallToleranceChunks чанков
    // чтения — мелкие пропуски записи (джиттер комиттера вывода) их терпят.
    static constexpr int kFineWindowFrames = 1200;  // 50 мс при 24 кГц
    static constexpr int kFineMaxLagFrames = 4800;  // 200 мс: очередь DMA + акустика
    static constexpr int kFineLagStepFrames = 48;   // 2 мс
    static constexpr int kFineStallToleranceChunks = 5;
    static constexpr int64_t kFineScanCooldownFrames = 72000;  // не чаще 3 с
    static constexpr int64_t kLagWindowFrames = 240000;        // отчёт раз в 10 с
    int64_t last_ref_head_ = 0;      // позиция кольца на начале прошлого чанка
    int64_t run_start_ref_head_ = 0;  // позиция кольца на начале непрерывного прогона
    int stall_chunks_ = 0;            // чанков чтения подряд без записи опоры
    bool saved_input_gain_ = false;   // в NVS есть ручное усиление микрофона
    int64_t last_scan_at_ = -1000000;
    int64_t lag_mic_energy_ = 0;
    int64_t lag_ref_energy_ = 0;
    int64_t lag_window_frames_ = 0;
    int64_t lag_ref_write_start_ = 0;    // позиция записи кольца на начале окна
    bool lag_locked_ = false;

    void TrackEchoAlignment(const int16_t* mic, int frames);
    int MaxScanLagLocked() const;  // сколько гипотез сейчас позволяет прогон
    void ReportWindowLocked(int64_t run_frames, int max_lag);
    void ScanEchoDelayFineLocked(int max_lag_frames);

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout,
                              gpio_num_t din);
    void ResetCodec();
    void UpdateDeviceState();
    bool StartSoftwareReference();
    int16_t TakeReferenceLocked();

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    Es8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate,
                     int output_sample_rate, gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                     gpio_num_t dout, gpio_num_t din, gpio_num_t pa_pin, uint8_t es8311_addr,
                     bool use_mclk = true, bool pa_inverted = false,
                     bool input_reference = false);
    virtual ~Es8311AudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void SetInputGain(float gain) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;

    // Диагностика: состояние кодека + дамп регистров ES8311 (0x00..0x49)
    // через ctrl_if (esp_codec_dev), без низкоуровневого es8311_handle_t.
    // Не блокирует мьютекс: вызывать ВНЕ переходов вкл/выкл ввода-вывода
    // (внутри UpdateDeviceState можно). Только для отладки.
    void LogDiagnostics();

    // Итог по оценке задержки эха (r — нормированная корреляция, чем ближе
    // к 1, тем увереннее пик). Вызывается автоматически из Read() раз в
    // kLagWindowFrames; публичный вариант — для кнопки BOOT/отладки.
    void LogEchoAlignment();

    // === Ручная подстройка опорного канала (экран «Настройка эха») ===
    // Всё потокобезопасно: те же данные крутит коррелятор из Read().
    // Значения отдаются в миллисекундах — так их показывает экран, и так их
    // понятнее читать в логе; внутри всё в кадрах по output_sample_rate_.
    int EchoDelayMs();
    // Выставить задержку вручную. Снимает автопоиск: ручное значение всегда
    // старше, иначе коррелятор на следующем громком слове перешибёт настройку.
    void SetEchoDelayMs(int ms);
    // Усилитель/микрофон: true, если в NVS уже лежит сохранённая пользователем
    // чувствительность входа. Борд в этом случае НЕ должен перебивать её
    // константой AUDIO_INPUT_GAIN.
    bool HasSavedInputGain() const {
        return saved_input_gain_;
    }
    int RefToleranceMs();
    // Насколько свободно опорному каналу «гулять» относительно воспроизведения
    // (перебуферизация DMA, паузы). Слишком мало — опорный канал будет дёргаться,
    // слишком много — в AEC поедёт рассогласование.
    void SetRefToleranceMs(int ms);
    // Включить коррелятор заново: он сам найдёт пик эха и зафиксирует задержку.
    void RestartEchoScan();
    // Возвращает true, если коррелятор уже нашёл пик; в *ms — измеренное значение.
    bool EchoDelayMeasured(int* ms);
    bool EchoDelayManual();
    // Прочитать/записать сохранённые в NVS (неймспейс echo_tune) значения.
    // Загрузку вызывает конструктор, сохранение — экран настроек.
    void LoadEchoTuning();
    void SaveEchoTuning();
};

#endif  // _ES8311_AUDIO_CODEC_H
