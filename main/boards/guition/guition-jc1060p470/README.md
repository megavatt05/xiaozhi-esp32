# Guition JC1060P470C (I_W / _Y) — ESP32-P4 v1.3

Поддержка платы **GUITION JC1060P470C** для xiaozhi-esp32.
Пины и команды панели сверены с официальным портом xiaozhi-esp32 от Guition
([guitionofficial/P4-series](https://github.com/guitionofficial/P4-series));
тайминги DPI и таблица init-команд дополнительно проверены на железе.

## Аппаратура

| Модуль   | Описание |
|----------|----------|
| MCU      | **ESP32-P4 rev 1.3** (engineering sample; также 1.0) |
| Wi-Fi/BLE| ESP32-C6 через ESP-Hosted (SDIO) |
| Дисплей  | 7″ IPS 1024×600, **JD9165**, MIPI-DSI 2 lane |
| Тач      | **GT911** (I²C, шина общая с ES8311) |
| Аудио    | **ES8311** + PA (GPIO20) |
| Камера   | Нет (вендорские демо используют USB UVC-камеру) |
| SD       | Не используется: SDMMC slot 0 занят ESP-Hosted (C6) |

## ESP32-P4 v1.3 — обязательные флаги

В `sdkconfig.defaults.esp32p4` и `config.json` уже задано:

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

Без этого на eng sample (1.0/1.3) прошивка не загрузится (Illegal Instruction).
`REV_MIN_100` также разрешает флеш 80 МГц (при `REV_MIN_0` дефолт — 40 МГц).

Таблица разделов и offset (0x8000, `partitions/v2/16m.csv`) заданы в общем
`sdkconfig.defaults` — продублировать нигде не нужно.

## Особенности реализации

1. **Init-команды JD9165** — вендорская таблица ~45 команд зашита в
   `guition-jc1060p470.cc` и передаётся в `jd9165_vendor_config_t.init_cmds`.
   Без неё драйвер использует generic-набор (на части панелей — «серый» экран).
2. **Тайминги DPI** — проверены на железе: 52 МГц, H 160/24/160, V 21/2/12,
   lane 750 Мбит/с. Если экран «чёрный» — README вендора советует перебрать
   `lane_bit_rate_mbps`: 550 / 750 / 900.
3. **Тач GT911** — `mirror_x=1, mirror_y=1` (как в вендорском порту для
   1024×600), адрес по умолчанию 0x5D, шина 400 кГц.
4. **Кнопка BOOT — GPIO35** (GPIO34..38 — strapping-пины ESP32-P4).
   В вендорском шаблоне указано GPIO21 — это ошибка шаблона: GPIO21 занят
   прерыванием тача.

## Сборка

```bash
idf.py set-target esp32p4        # или просто idf.py build — цель по умолчанию esp32p4
idf.py menuconfig                # Xiaozhi Assistant → Board Type → Guition JC1060P470C
idf.py build
idf.py -p PORT flash monitor
```

Зависимость `espressif/esp_lcd_jd9165 ^2.0.2` уже прописана в
`main/idf_component.yml` (только для target esp32p4) — вручную ничего
добавлять не нужно.

## Пины (офиц. Guition)

| Функция | GPIO |
|---------|------|
| LCD_RST | 27 |
| Backlight (PWM) | 23 |
| MIPI PHY LDO | ch3 @ 2.5 В (включать до init DSI) |
| I2C SDA/SCL | 7 / 8 |
| GT911 RST/INT | 22 / 21 |
| I2S MCLK/WS/BCLK/DOUT/DIN | 13 / 10 / 12 / 9 / 11 |
| PA | 20 |
| BOOT | 35 |

## Build note: ESP-IDF 6.0.2 + esp_lvgl_port

Если сборка падает с ошибкой
`esp_lcd_dpi_panel_event_callbacks_t has no member named on_frame_buf_complete` —
это несовместимость esp_lvgl_port 2.9.0 с callback API IDF 6.0.2. После
`idf.py reconfigure` / первой неудачной сборки запустите:

```bash
bash scripts/fix_esp_lvgl_port_idf602.sh     # или powershell scripts/fix_esp_lvgl_port_idf602.ps1
idf.py build
```

(В IDF 6.0.2 колбэк называется `on_refresh_done`; `on_frame_buf_complete`
появился в более новых версиях IDF.)

## Источники

- https://github.com/guitionofficial/P4-series — официальный порт (платы
  `guition-jc1060p470`, `guition-jc8012p4a1`, `guition-jc-esp32p4-m3-dev`)
- https://github.com/megavatt05/P4-series — зеркало Guition
- https://github.com/megavatt05/ESP32P4-JC1060P470C-I_W_Y — проверка дисплея
  на железе (ветки jd9165_hello_display, esp_brookesia_phone)
