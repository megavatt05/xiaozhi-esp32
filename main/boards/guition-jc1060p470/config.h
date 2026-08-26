#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_types.h>

/*
 * GUITION JC1060P470C_I_W (and _Y)
 * ESP32-P4 rev 1.0/1.3 + ESP32-C6, 7" 1024x600 JD9165 MIPI-DSI,
 * GT911 touch, ES8311 audio, MIPI-CSI camera.
 *
 * Pinout derived from official package + community (ESPHome, LVGL demos).
 */

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_13
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_12
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_10
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_9
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_48
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_11   /* AMP_EN, active high */
#define AUDIO_CODEC_I2C_PORT     I2C_NUM_1
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_8

#define BOOT_BUTTON_GPIO         GPIO_NUM_0

#define DISPLAY_WIDTH            1024
#define DISPLAY_HEIGHT           600
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false

#define DISPLAY_RESET_PIN        GPIO_NUM_5
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_23
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define LCD_MIPI_DSI_LANE_NUM           2
#define LCD_MIPI_DSI_LANE_BITRATE_MBPS  900
#define MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

/* GT911 touch (shared I2C with ES8311) */
#define TOUCH_RST_GPIO           GPIO_NUM_22
#define TOUCH_INT_GPIO           GPIO_NUM_21

#define CAMERA_RESET_PIN         GPIO_NUM_NC
#define CAMERA_PWDN_PIN          GPIO_NUM_NC

#define SD_CARD_MOUNT_POINT      "/sdcard"
#define SD_CARD_PWR_LDO_CHAN     4

#endif  // _BOARD_CONFIG_H_
