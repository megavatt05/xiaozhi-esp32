# Guition JC1060P470C — интеграция (ESP32-P4 v1.3)

Ветка: `feature/guition-jc1060p470`
Репозиторий: https://github.com/megavatt05/xiaozhi-esp32

## Состав ветки

| Путь | Описание |
|------|----------|
| `main/boards/guition-jc1060p470/config.h` | Пины платы (русские комментарии) |
| `main/boards/guition-jc1060p470/guition-jc1060p470.cc` | Класс платы: JD9165 + GT911 + ES8311 |
| `main/boards/guition-jc1060p470/config.json` | Метаданные + sdkconfig_append |
| `main/boards/guition-jc1060p470/README.md` | Описание платы, пины, сборка |
| `main/Kconfig.projbuild` | Опция `BOARD_TYPE_GUITION_JC1060P470` + плата по умолчанию для ESP32-P4 |
| `main/CMakeLists.txt` | Блок `BOARD_DIR "guition-jc1060p470"` (шрифты как у соседних P4-плат) |
| `main/idf_component.yml` | `espressif/esp_lcd_jd9165 ^2.0.2` (только esp32p4) |
| `sdkconfig.defaults` | `CONFIG_IDF_TARGET="esp32p4"` — сборка без `set-target` |
| `sdkconfig.defaults.esp32p4` | Ревизия v1.0/v1.3, ESP-Hosted SDIO, аудио/речь |
| `scripts/fix_esp_lvgl_port_idf602.sh/.ps1` | Workaround esp_lvgl_port 2.9.0 vs IDF 6.0.2 |

## Ключевые решения (почему так)

1. **Ревизия v1.0/v1.3**: `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` +
   `CONFIG_ESP32P4_REV_MIN_100=y`. Без них — Illegal Instruction на
   engineering sample. Опция REV_MIN_100 действует ТОЛЬКО вместе с
   SELECTS_REV_LESS_V3 (иначе молча игнорируется, дефолт становится v3.1).
2. **Init-команды JD9165** из официального порта Guition передаются явно
   (`jd9165_vendor_config_t.init_cmds`) — проверены на железе.
3. **Тайминги DPI**: 52 МГц, H 160/24/160, V 21/2/12, lane 750 Мбит/с —
   проверены на железе. В вендорском порту lane = 900 и vsync_pulse_width = 10.
4. **BOOT = GPIO35** (strapping-пин P4); GPIO21 в вендорском шаблоне — ошибка
   (там реально сидит INT тача GT911).
5. **SD-карта удалена**: SDMMC slot 0 занят ESP-Hosted (канал до C6) —
   инициализация карты конфликтовала бы с Wi-Fi.
6. **Камера удалена**: MIPI-CSI сенсор на плате отсутствует (вендорские демо
   камеры — USB UVC); `GetCamera()` базового класса вернёт nullptr.
7. **esp_lcd_jd9165 ^2.0.2** (реестр компонентов) вместо вендорского 1.0.2:
   поддерживает IDF 5.5/6.0 и принимает init-команды вендора.

## Сборка (IDF 5.5.x)

```bash
git clone -b feature/guition-jc1060p470 https://github.com/megavatt05/xiaozhi-esp32.git
cd xiaozhi-esp32
idf.py set-target esp32p4     # можно опустить: цель по умолчанию esp32p4
idf.py build
idf.py -p PORT flash monitor
```

При чистой пересборке удаляйте `managed_components/`, `dependencies.lock`,
`sdkconfig*` и `build/`.

Для IDF 6.0.2 см. раздел «Build note» в README платы (патч esp_lvgl_port).
