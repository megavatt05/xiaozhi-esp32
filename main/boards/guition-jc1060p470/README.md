# Guition JC1060P470C (I_W / I_W_Y) — ESP32-P4 **v1.3**

Поддержка платы **GUITION JC1060P470C** для xiaozhi-esp32.

## Аппаратура

| Модуль | Описание |
|--------|----------|
| MCU | **ESP32-P4 rev 1.3** (engineering sample; также 1.0) |
| Wi-Fi / BLE | ESP32-C6 через ESP-Hosted (SDIO) |
| Дисплей | 7″ IPS 1024×600, **JD9165**, MIPI-DSI 2-lane |
| Тач | **GT911** (I²C) |
| Аудио | **ES8311** + PA |
| Камера | MIPI-CSI |
| SD | TF (конфликт с Wi-Fi C6) |

## ESP32-P4 v1.3 — обязательные флаги

В `config.json` / `sdkconfig.defaults.esp32p4` уже задано:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

Без этого на eng sample (1.0/1.3) возможен **Illegal Instruction**.

Также: `CONFIG_PARTITION_TABLE_OFFSET=0x10000`, Flash 16 MB, PSRAM + XIP.

## Восстановление Kconfig / CMakeLists

После клонирования ветки:

```bash
bash main/boards/guition-jc1060p470/patches/APPLY.sh
```

Или вручную:

```bash
git checkout main -- main/Kconfig.projbuild main/CMakeLists.txt
patch -p1 < main/boards/guition-jc1060p470/patches/kconfig.guition.patch
patch -p1 < main/boards/guition-jc1060p470/patches/cmake.guition.patch
```

## Сборка (IDF 5.5.x)

```bash
idf.py set-target esp32p4
idf.py add-dependency "espressif/esp_lcd_jd9165^1.0.3"
idf.py menuconfig
# Xiaozhi Assistant → Board Type → Guition JC1060P470C 7" ...
idf.py build
idf.py -p PORT flash monitor
```

## Пины (офиц. Guition)

| Функция | GPIO |
|---------|------|
| LCD_RST | 27 |
| Backlight | 23 |
| I2C SDA/SCL | 7 / 8 |
| GT911 RST/INT | 22 / 21 |
| I2S | 13/12/10/9/11 |
| PA | 20 |
| BOOT | 35 |
| MIPI PHY LDO | ch3 @ 2.5 V |

## Важно

1. Chip revision: Minimum Supported **Rev v1.0**, не только ≥3.0  
2. LDO channel 3 = 2.5 V до init MIPI-DSI  
3. SD и Wi-Fi C6 делят SDIO  
4. Логи: USB High-Speed  

## Источники

- https://github.com/guitionofficial/P4-series  
- https://github.com/megavatt05/P4-series  
- waveshare/esp32-p4-wifi6-touch-lcd (API reference)  
