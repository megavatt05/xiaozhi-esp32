# Guition JC1060P470C (I_W / I_W_Y)

Поддержка платы **GUITION JC1060P470C** для xiaozhi-esp32.

## Аппаратура

| Модуль | Описание |
|--------|----------|
| MCU | ESP32-P4 (rev 1.0 / 1.3, engineering sample) |
| Wi-Fi / BLE | ESP32-C6 через ESP-Hosted (SDIO) |
| Дисплей | 7″ IPS 1024×600, **JD9165**, MIPI-DSI 2-lane |
| Тач | **GT911** (I²C) |
| Аудио | **ES8311** + PA |
| Камера | MIPI-CSI |
| SD | TF (конфликт с Wi-Fi C6) |

Пины — по официальному пакету Guition `P4-series` / демо xiaozhi.

## Сборка (IDF 5.5.x)

```bash
idf.py set-target esp32p4
idf.py add-dependency "espressif/esp_lcd_jd9165^1.0.3"
idf.py menuconfig
# Xiaozhi Assistant → Board Type → Guition JC1060P470C 7" ...
idf.py build
idf.py -p PORT flash monitor
```

## Важно

1. Chip revision: Minimum Supported **Rev v1.0**, не только ≥3.0  
2. LDO channel 3 = 2.5 V до init MIPI-DSI  
3. SD и Wi-Fi C6 делят SDIO — при активном Wi-Fi SD может не монтироваться  
4. Логи: предпочтительно USB High-Speed  

## Источники

- Официальный пакет: https://github.com/guitionofficial/P4-series  
- Форк документации: https://github.com/megavatt05/P4-series  
- Референс board API: waveshare/esp32-p4-wifi6-touch-lcd в upstream xiaozhi-esp32  
