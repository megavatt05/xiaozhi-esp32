/*
 * =============================================================================
 *  Плата: GUITION JC1060P470C (JC1060P470C_I_W / _Y)
 * =============================================================================
 *  Аппаратура:
 *    - ESP32-P4 (engineering sample v1.0 / v1.3) + ESP32-C6 (Wi-Fi 6, ESP-Hosted)
 *    - Дисплей: 7" IPS 1024x600, JD9165, MIPI-DSI 2 lane
 *    - Тач: GT911 (I2C, шина общая с ES8311)
 *    - Аудио: ES8311 + усилитель (PA на GPIO20)
 *
 *  Источник пинов и команд панели: официальный порт xiaozhi-esp32 от Guition
 *  (guitionofficial/P4-series, плата guition-jc1060p470). Тайминги DPI и
 *  таблица init-команд дополнительно проверены на железе (см. репозиторий
 *  megavatt05/ESP32P4-JC1060P470C-I_W_Y, ветка jd9165_hello_display).
 *
 *  IDF: 5.5.x (основной) и 6.0.x (через #if ветки ниже)
 *  Зависимость панели: espressif/esp_lcd_jd9165 ^2.0.2 — уже прописана в
 *  main/idf_component.yml, вручную add-dependency НЕ нужен.
 * =============================================================================
 */

#include "application.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "button.h"
#include "config.h"
#include "display/display.h"
#include "display/lcd_display.h"
#include "lvgl_theme.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <esp_idf_version.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "esp_lcd_jd9165.h"
#include "esp_lcd_touch_gt911.h"

#define TAG "GuitionJC1060P470"

/*
 * Таблица инициализации JD9165 от вендора (Guition).
 * Порядок полей jd9165_lcd_init_cmd_t: {cmd, data, data_bytes, delay_ms}.
 * Проверена на этой панели; без неё драйвер использует внутренний
 * generic-инициализационный набор — на некоторых партиях панели он даёт
 * неверные gamma/напряжения (пустой или "серый" экран).
 */
static const jd9165_lcd_init_cmd_t jd9165_gui_init_cmds[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49, 0x61, 0x02, 0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},
    // Выход из сна и включение дисплея (с паузами по спецификации)
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

class GuitionJC1060P470Board : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_ = nullptr;
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    esp_lcd_dsi_bus_handle_t dsi_bus_ = nullptr;
    esp_ldo_channel_handle_t dsi_phy_power_ = nullptr;
    esp_lcd_touch_handle_t tp_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_ = nullptr;
    lv_indev_t* touch_indev_ = nullptr;

    // Единая шина I2C: ES8311 (аудио) + GT911 (тач), пины см. config.h
    void InitializeI2cBus() {
        i2c_master_bus_config_t i2c_bus_config = {
            .i2c_port = AUDIO_CODEC_I2C_PORT,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &codec_i2c_bus_));
        ESP_LOGI(TAG, "Шина I2C готова (SDA=%d SCL=%d)",
                 AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
    }

    // Питание MIPI-DSI PHY: LDO канал 3, 2500 мВ — ДО инициализации шины
    void EnableDsiPhyPower() {
        esp_ldo_channel_config_t ldo_config = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_config, &dsi_phy_power_));
        ESP_LOGI(TAG, "MIPI DSI PHY: LDO ch%d @ %d мВ", MIPI_DSI_PHY_PWR_LDO_CHAN,
                 MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV);
    }

    void InitializeLcd() {
        EnableDsiPhyPower();

        // Шина MIPI-DSI: 2 data lane.
        // Скорость линии: 750 Мбит/с — проверено на железе (совпадает с
        // макросом JD9165_PANEL_BUS_DSI_2CH_CONFIG компонента). Вендорский
        // порт использует 900; если экран "чёрный" — README вендора советует
        // попробовать 550/750/900.
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = LCD_MIPI_DSI_LANE_NUM,
            .lane_bit_rate_mbps = LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        };
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus_));

        // Командный канал DBI: 8 бит команда / 8 бит параметр
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_dbi_io_config_t dbi_config = {
            .virtual_channel = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus_, &dbi_config, &panel_io));

        // Тайминги DPI — проверены на этой панели (52 МГц, H 24/160/160,
        // V 2/21/12). Альтернативы:
        //   - макрос компонента JD9165_1024_600_PANEL_60HZ_DPI_CONFIG:
        //     50 МГц, H 136/20/160, V 12/2/20;
        //   - вендорский порт xiaozhi: 52 МГц, те же porch, но vsync_pulse_width=10.
        // Если картинка "поедет" — замените porch'и на один из вариантов выше.
        // ВАЖНО (IDF 6.x): порядок полей esp_lcd_dpi_panel_config_t изменился —
        // virtual_channel идёт первым и in_color_format заменил pixel_format,
        // поэтому config собираем по полям, а не одним designated-initializer.
        esp_lcd_dpi_panel_config_t dpi_config = {};
        dpi_config.virtual_channel = 0;
        dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        dpi_config.dpi_clock_freq_mhz = LCD_DPI_CLOCK_MHZ;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        dpi_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
        dpi_config.num_fbs = 1;
        dpi_config.video_timing = {
            .h_size = DISPLAY_WIDTH,
            .v_size = DISPLAY_HEIGHT,
            .hsync_pulse_width = 24,
            .hsync_back_porch = 160,
            .hsync_front_porch = 160,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 21,
            .vsync_front_porch = 12,
        };
        dpi_config.flags.use_dma2d = true;
#else
        dpi_config.in_color_format = LCD_COLOR_FMT_RGB565;
        dpi_config.num_fbs = 1;
        dpi_config.video_timing = {
            .h_size = DISPLAY_WIDTH,
            .v_size = DISPLAY_HEIGHT,
            .hsync_pulse_width = 24,
            .hsync_back_porch = 160,
            .hsync_front_porch = 160,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 21,
            .vsync_front_porch = 12,
        };
#endif

        // Вендорская конфигурация панели: наши init-команды + DPI-конфиг
        jd9165_vendor_config_t vendor_config = {
            .init_cmds = jd9165_gui_init_cmds,
            .init_cmds_size = sizeof(jd9165_gui_init_cmds) / sizeof(jd9165_lcd_init_cmd_t),
            .mipi_config = {
                .dsi_bus = dsi_bus_,
                .dpi_config = &dpi_config,
            },
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RESET_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = &vendor_config;

        esp_lcd_panel_handle_t panel = nullptr;
        ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(panel_io, &panel_config, &panel));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        // В IDF 6.x use_dma2d убрали из dpi_config — DMA2D включается отдельно
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_enable_dma2d(panel));
#endif
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

        display_ = new MipiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                      DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        ESP_LOGI(TAG, "Дисплей JD9165 1024x600 инициализирован");
    }

    // Кнопка BOOT (GPIO35, strapping): короткое нажатие — старт/стоп диалога,
    // в режиме начальной настройки — вход в режим конфигурации Wi-Fi
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTouch() {
        // Ориентация GT911: для этой панели 1024x600 вендор использует
        // mirror_x=1, mirror_y=1 (см. порт Guition; swap_xy=0)
        esp_lcd_touch_config_t touch_config = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = TOUCH_RST_GPIO,
            .int_gpio_num = TOUCH_INT_GPIO,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 1,
                .mirror_y = 1,
            },
        };
        // GT911: адрес из макроса по умолчанию (0x5D), 400 кГц — как на
        // платах Waveshare P4; шина общая с ES8311
        esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        touch_io_config.scl_speed_hz = 400000;

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(codec_i2c_bus_, &touch_io_config, &touch_io_));
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(touch_io_, &touch_config, &tp_));

        // Тач нужно привязать к уже созданному LVGL-дисплею
        lv_display_t* lv_display = lv_display_get_default();
        if (lv_display == nullptr) {
            ESP_LOGE(TAG, "LVGL-дисплей не создан — тач не зарегистрирован");
            return;
        }
        const lvgl_port_touch_cfg_t lv_touch_config = {
            .disp = lv_display,
            .handle = tp_,
        };
        touch_indev_ = lvgl_port_add_touch(&lv_touch_config);
        if (touch_indev_ == nullptr) {
            ESP_LOGE(TAG, "Не удалось зарегистрировать GT911 в LVGL");
        } else {
            ESP_LOGI(TAG, "Тач GT911 зарегистрирован");
        }
    }

public:
    GuitionJC1060P470Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2cBus();
        InitializeLcd();
        InitializeButtons();
        InitializeTouch();
        GetBacklight()->RestoreBrightness();
        ESP_LOGI(TAG, "Плата Guition JC1060P470C готова");
    }

    ~GuitionJC1060P470Board() {
        // Разборка в порядке, обратном созданию
        if (touch_indev_ != nullptr) {
            lvgl_port_remove_touch(touch_indev_);
            touch_indev_ = nullptr;
        }
        if (tp_ != nullptr) {
            esp_lcd_touch_del(tp_);
            tp_ = nullptr;
        }
        if (touch_io_ != nullptr) {
            esp_lcd_panel_io_del(touch_io_);
            touch_io_ = nullptr;
        }

        delete display_;
        display_ = nullptr;

        if (dsi_bus_ != nullptr) {
            esp_lcd_del_dsi_bus(dsi_bus_);
            dsi_bus_ = nullptr;
        }
        if (dsi_phy_power_ != nullptr) {
            esp_ldo_release_channel(dsi_phy_power_);
            dsi_phy_power_ = nullptr;
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        // use_mclk=true, pa_inverted=false (значения по умолчанию — явно)
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_, AUDIO_CODEC_I2C_PORT, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, true, false);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        // ШИМ-подсветка на GPIO23 (LEDC)
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    // Камеры на плате нет: MIPI-CSI интерфейс продукта не распаян под сенсор
    // (вендорские демо используют USB UVC-камеру) — GetCamera() из базового
    // класса вернёт nullptr, приложение штатно отключит функцию камеры.
};

DECLARE_BOARD(GuitionJC1060P470Board);
