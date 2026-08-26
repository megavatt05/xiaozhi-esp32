# GUITION JC1060P470C (JC1060P470C_I_W / _Y)

7-дюймовая HMI-плата 1024×600 IPS на **ESP32-P4** (инженерный образец v1.0/v1.3) + **ESP32-C6** (Wi-Fi 6 / BT).

## Реализовано

| Функция | Детали |
|---------|--------|
| Дисплей | JD9165 MIPI-DSI 1024×600, 2-lane, 900 Мбит/с |
| Тач | GT911 (I²C, RST/INT) |
| Аудио | ES8311 + усилитель (PA=GPIO20) |
| Камера | MIPI-CSI через EspVideo |
| Wi-Fi | ESP-Hosted (SDIO) → ESP32-C6 |
| Ревизия чипа | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` + min v1.0 |

## Пины (официальное демо Guition + расширения)

| Функция | GPIO |
|---------|------|
| LCD_RST | 27 |
| Подсветка (PWM) | 23 |
| I2C SDA / SCL | 7 / 8 |
| GT911 RST / INT | 22 / 21 |
| I2S MCLK/BCLK/WS/DOUT | 13/12/10/9 |
| I2S DIN (микрофон) | 11 |
| AMP_EN (PA) | 20 |
| BOOT | 35 |
| LED | 26 |
| MIPI PHY LDO | канал 3 @ 2.5 В |

## Зависимости

```bash
idf.py add-dependency "espressif/esp_lcd_jd9165^1.0.2"
```

IDF: **5.5.x** (проверено для 5.5.5). Компонент JD9165 требует IDF ≥ 5.3.

## Важно

1. **Инженерный образец P4** — без флагов ревизии чип падает с Illegal Instruction.
2. **SDIO-конфликт** — TF-карта и C6 делят SDIO. При активном Wi-Fi SD лучше не монтировать.
3. **USB-логи** — порт USB High-Speed + `USB_SERIAL_JTAG`.
4. Тач RST/INT — community-значения; при отсутствии реакции проверьте схему своей ревизии.

## Сборка

```bash
idf.py set-target esp32p4
idf.py menuconfig   # Xiaozhi Assistant → Board Type → Guition JC1060P470C
idf.py build
```

`config.json` автоматически добавляет нужные опции Hosted и ревизии.

## Источники

- Официальный пакет: https://github.com/Aidariki/P4-series/tree/master/JC1060P470C_I_W/.../examples/xiaozhi
- Ближайший upstream-референс: `main/boards/espressif/esp32-p4-function-ev-board`
