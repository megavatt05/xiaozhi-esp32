#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/*
 * GUITION JC1060P470C_I_W / _Y
 * ESP32-P4 engineering sample: ревизия 1.0 / **1.3**
 *
 * Требуемые опции sdkconfig (уже заданы в sdkconfig.defaults.esp32p4):
 *   CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
 *   CONFIG_ESP32P4_REV_MIN_100=y
 * Без них на eng sample (v1.0/v1.3) прошивка не загрузится (Illegal Instruction).
 *
 * Аудио-пины сверены со СХЕМОЙ JC1060P470C_I_W_Y-V1.0 (5-Schematic
 * вендорского пакета guitionofficial/P4-series) и с рабочим BSP
 * ESP32P4-JC1060P470C-I_W_Y (ветка esp_brookesia_phone, звук подтверждён
 * на железе):
 *   ES8311 ASDOUT (данные микрофона) -> GPIO48 (сеть "ES7210_SDOUT" —
 *     унаследованное имя от ESP32-P4-Function-EV-Board, физически это
 *     выход АЦП самой ES8311; отдельного чипа ES7210 на плате НЕТ,
 *     блок ADC&AEC содержит только аналоговый микрофон MSM381A);
 *   PA_CTRL (STD усилителя NS4150, активный HIGH, R19 10K pull-down) -> GPIO11.
 * Прежние значения DIN=GPIO11/PA=GPIO20 были перепутаны местами:
 *   DIN=11 читал линию PA (pull-down -> постоянный ноль => "запись тишины",
 *   wake word не срабатывал), PA=20 ни к чему не подключён.
 */

// --- Аудио: ES8311, I2S дуплекс, 24 кГц (стандарт текущих плат xiaozhi) ---
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_13
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_12
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_9
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_48  // ES8311 ASDOUT (микрофон), см. схему

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_11  // PA_CTRL -> STD NS4150
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_8
#define AUDIO_CODEC_I2C_PORT     I2C_NUM_0
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

/*
 * Кнопка BOOT — GPIO35.
 * GPIO34..38 — strapping-пины ESP32-P4 (документация IDF); GPIO35 —
 * стандартная кнопка BOOT для модулей P4.
 * ВНИМАНИЕ: в вендорском шаблоне Guition BOOT=GPIO21 — это ошибка шаблона:
 * GPIO21 реально занят прерыванием тача (см. ниже), кнопка и INT не могут
 * делить один вывод.
 */
#define BOOT_BUTTON_GPIO         GPIO_NUM_35

// --- Дисплей: 7" IPS 1024x600, JD9165, MIPI-DSI 2 lane ---
#define DISPLAY_WIDTH            1024
#define DISPLAY_HEIGHT           600
#define DISPLAY_RESET_PIN        GPIO_NUM_27
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_23
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0

#define LCD_MIPI_DSI_LANE_NUM          2
#define LCD_MIPI_DSI_LANE_BITRATE_MBPS 750   // проверено на железе (вендор: 900; при чёрном экране пробуйте 550/750/900)
#define LCD_DPI_CLOCK_MHZ              52    // проверено на железе (макрос компонента: 50)

// Питание MIPI-DSI PHY — LDO канал 3, 2500 мВ, включать ДО инициализации
#define MIPI_DSI_PHY_PWR_LDO_CHAN      3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

// --- Тач: GT911 на общей с ES8311 шине I2C (SDA=7, SCL=8) ---
#define TOUCH_RST_GPIO           GPIO_NUM_22
#define TOUCH_INT_GPIO           GPIO_NUM_21

#endif /* _BOARD_CONFIG_H_ */
