#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_types.h>

/*
 * =============================================================================
 *  GUITION JC1060P470C_I_W / _Y
 *  ESP32-P4 (инженерный образец v1.0 / v1.3) + ESP32-C6 (Wi-Fi 6 / BT)
 *  7" IPS 1024×600, контроллер JD9165 (MIPI-DSI 2-lane)
 *  Тач: GT911, Аудио: ES8311, Камера: MIPI-CSI
 * =============================================================================
 *  Пины взяты из официального пакета Guition:
 *  P4-series/.../examples/xiaozhi/jc1060p470/config.h
 *  + расширения для тача и камеры (пользовательский запрос).
 * =============================================================================
 */

/* ---------- Аудио (ES8311) ---------- */
#define AUDIO_INPUT_SAMPLE_RATE   24000
#define AUDIO_OUTPUT_SAMPLE_RATE  24000

#define AUDIO_I2S_GPIO_MCLK       GPIO_NUM_13
#define AUDIO_I2S_GPIO_BCLK       GPIO_NUM_12
#define AUDIO_I2S_GPIO_WS         GPIO_NUM_10
#define AUDIO_I2S_GPIO_DOUT       GPIO_NUM_9
#define AUDIO_I2S_GPIO_DIN        GPIO_NUM_11   /* микрофон (офиц. демо) */
#define AUDIO_CODEC_PA_PIN        GPIO_NUM_20   /* усиление динамика (офиц. демо) */
#define AUDIO_CODEC_I2C_PORT      I2C_NUM_1
#define AUDIO_CODEC_I2C_SDA_PIN   GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN   GPIO_NUM_8

/* ---------- Кнопка и LED ---------- */
#define BOOT_BUTTON_GPIO          GPIO_NUM_35
#define BUILTIN_LED_GPIO          GPIO_NUM_26

/* ---------- Дисплей JD9165 1024×600 ---------- */
#define DISPLAY_WIDTH             1024
#define DISPLAY_HEIGHT            600
#define DISPLAY_OFFSET_X          0
#define DISPLAY_OFFSET_Y          0
#define DISPLAY_MIRROR_X          false
#define DISPLAY_MIRROR_Y          false
#define DISPLAY_SWAP_XY           false

#define DISPLAY_RESET_PIN         GPIO_NUM_27   /* LCD_RST (офиц. демо) */
#define DISPLAY_BACKLIGHT_PIN     GPIO_NUM_23
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

/* MIPI-DSI: 2 линии данных, 900 Мбит/с (как в офиц. демо) */
#define LCD_MIPI_DSI_LANE_NUM            2
#define LCD_MIPI_DSI_LANE_BITRATE_MBPS   900
#define MIPI_DSI_PHY_PWR_LDO_CHAN        3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV  2500

/* ---------- Тач GT911 (общая I²C шина с кодеком) ---------- */
/* RST/INT — типичные значения community; при отсутствии реакции
 * попробуйте GPIO_NUM_NC или другие пины по схеме вашей ревизии. */
#define TOUCH_RST_GPIO            GPIO_NUM_22
#define TOUCH_INT_GPIO            GPIO_NUM_21

/* ---------- Камера MIPI-CSI ---------- */
#define CAMERA_RESET_PIN          GPIO_NUM_NC
#define CAMERA_PWDN_PIN           GPIO_NUM_NC

/* ---------- SD-карта (делит SDIO с C6 — конфликт с Wi-Fi) ---------- */
#define SD_CARD_MOUNT_POINT       "/sdcard"
#define SD_CARD_PWR_LDO_CHAN      4

#endif  // _BOARD_CONFIG_H_
