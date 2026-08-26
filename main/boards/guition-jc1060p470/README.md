# GUITION JC1060P470C (JC1060P470C_I_W / _Y)

7-inch 1024×600 IPS capacitive touch HMI board based on **ESP32-P4** (engineering sample v1.0 / v1.3) + **ESP32-C6** (Wi-Fi 6 / BT).

## Features implemented

| Feature        | Details                                      |
|----------------|----------------------------------------------|
| Display        | JD9165 MIPI-DSI 1024×600, 2-lane             |
| Touch          | GT911 (I²C, RST=GPIO22, INT=GPIO21)          |
| Audio          | ES8311 + amp (AMP_EN=GPIO11), mic + speaker  |
| Camera         | MIPI-CSI (EspVideo), typically OV02C10-class |
| Wi-Fi / BT     | ESP-Hosted over SDIO to on-board ESP32-C6    |
| Chip revision  | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` + min v1.0 |
| PSRAM          | 32 MB, XIP from PSRAM enabled                |

## Critical notes

1. **Engineering sample** – without the revision flags the chip will crash with Illegal Instruction.
2. **LCD driver** – this board uses **JD9165**. The current implementation follows the same MIPI-DSI + DPI pattern as the official Function-EV board.  
   If the panel does not come up, add the component `esp_lcd_jd9165` (or the vendor init sequence from the Guition package `4-Driver_IC_Data_Sheet`) and adapt `InitializeLcd()`.
3. **SDIO conflict** – TF-card and C6 share the same SDIO lines. Prefer Ethernet or unmount SD when Wi-Fi is active.
4. **USB logs** – use the **USB High-Speed** port + `USB_SERIAL_JTAG` for reliable console output.
5. Partition table offset should be **0x10000**, first app partition ≥ **0x11000** (see project checklist).

## Pin summary (key)

| Function              | GPIO          |
|-----------------------|---------------|
| LCD_RST               | 5             |
| Backlight (PWM)       | 23            |
| I2C SDA / SCL         | 7 / 8         |
| GT911 RST / INT       | 22 / 21       |
| I2S MCLK/BCLK/WS/DOUT | 13/12/10/9    |
| I2S DIN               | 48            |
| AMP_EN                | 11            |
| C6 Reset / SDIO       | 54 + 14-19    |
| MIPI PHY LDO          | channel 3 @ 2.5 V |

## Build

```bash
idf.py set-target esp32p4
idf.py menuconfig   # Xiaozhi Assistant → Board Type → Guition JC1060P470
idf.py build
```

The `config.json` automatically appends the required Hosted + revision options.

## References

- Board package: https://github.com/Aidariki/P4-series/tree/master/JC1060P470C_I_W
- Skill / pinout notes in the project workspace
- Closest upstream reference: `main/boards/espressif/esp32-p4-function-ev-board`
