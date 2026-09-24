# Guition JC1060P470C — интеграция в xiaozhi-esp32

Ветка: `feature/guition-jc1060p470`
Репозиторий: https://github.com/megavatt05/xiaozhi-esp32

## Уже в ветке

- `main/boards/guition-jc1060p470/config.h` — пины (JD9165, GT911, ES8311)
- `main/boards/guition-jc1060p470/guition-jc1060p470.cc` — board class
- `main/boards/guition-jc1060p470/README.md`
- `main/boards/guition-jc1060p470/config.json`

## Нужно добавить в Kconfig / CMake (если ещё нет)

### main/Kconfig.projbuild

После `BOARD_TYPE_ESP32_P4_FUNCTION_EV_BOARD`:

```
    config BOARD_TYPE_GUITION_JC1060P470
        bool "Guition JC1060P470C 7\" (ESP32-P4 + C6, JD9165, GT911)"
        depends on IDF_TARGET_ESP32P4
```

### main/CMakeLists.txt

После блока ESP32_P4_FUNCTION_EV_BOARD:

```cmake
elseif(CONFIG_BOARD_TYPE_GUITION_JC1060P470)
    set(BOARD_DIR "guition-jc1060p470")
    set(BUILTIN_TEXT_FONT font_noto_sans_basic_30_4)
    set(BUILTIN_ICON_FONT font_material_symbols_30_4)
    set(DEFAULT_EMOJI_COLLECTION noto-color-emoji_64)
```

И в блоке sdmmc:

```cmake
if(CONFIG_BOARD_TYPE_ESP32_P4_FUNCTION_EV_BOARD OR CONFIG_BOARD_TYPE_GUITION_JC1060P470)
    list(APPEND MAIN_PRIV_REQUIRES_EXTRA
        esp_driver_sdmmc
        sdmmc
    )
endif()
```

Готовые файлы также лежат в артефактах проекта:
- `Kconfig.projbuild.guition`
- `CMakeLists.txt.guition`

## Сборка

```bash
git clone -b feature/guition-jc1060p470 https://github.com/megavatt05/xiaozhi-esp32.git
cd xiaozhi-esp32
# применить патчи Kconfig/CMake из docs или artifacts
idf.py set-target esp32p4
idf.py add-dependency "espressif/esp_lcd_jd9165^1.0.3"
idf.py menuconfig   # Board Type → Guition JC1060P470C
idf.py build flash monitor
```
