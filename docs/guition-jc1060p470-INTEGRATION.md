# Guition JC1060P470C — интеграция (ESP32-P4 **v1.3**)

Ветка: `feature/guition-jc1060p470`  
Репозиторий: https://github.com/megavatt05/xiaozhi-esp32

## Уже в ветке

| Путь | Описание |
|------|----------|
| `main/boards/guition-jc1060p470/config.h` | Пины + пометка v1.3 |
| `main/boards/guition-jc1060p470/guition-jc1060p470.cc` | Board class |
| `main/boards/guition-jc1060p470/config.json` | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`, `REV_MIN_100` |
| `sdkconfig.defaults.esp32p4` | Eng sample defaults |
| `main/boards/guition-jc1060p470/patches/*` | Патчи Kconfig/CMake + `APPLY.sh` |

## ESP32-P4 v1.3 (engineering sample)

Обязательные флаги (уже в `sdkconfig.defaults.esp32p4` и `config.json`):

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
CONFIG_PARTITION_TABLE_OFFSET=0x10000
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
```

Без `SELECTS_REV_LESS_V3` / `REV_MIN_100` на v1.0/v1.3 возможен **Illegal Instruction**.

## Восстановление полных Kconfig / CMakeLists

На ветке эти файлы могли быть усечены. Восстановление одной командой:

```bash
bash main/boards/guition-jc1060p470/patches/APPLY.sh
```

Скрипт:
1. `git checkout main -- main/Kconfig.projbuild main/CMakeLists.txt`
2. Накладывает патчи Guition (опция платы, BOARD_DIR, sdmmc)

Полные готовые файлы также в артефактах проекта:
- `Kconfig.projbuild.guition`
- `CMakeLists.txt.guition`

## Сборка (IDF 5.5.x)

```bash
git clone -b feature/guition-jc1060p470 https://github.com/megavatt05/xiaozhi-esp32.git
cd xiaozhi-esp32
bash main/boards/guition-jc1060p470/patches/APPLY.sh
idf.py set-target esp32p4
idf.py add-dependency "espressif/esp_lcd_jd9165^1.0.3"
idf.py menuconfig   # Board Type → Guition JC1060P470C
idf.py build flash monitor
```
