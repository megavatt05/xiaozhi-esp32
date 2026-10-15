#pragma once

#include <functional>
#include <string>
#include <vector>

#include <lvgl.h>

class Es8311AudioCodec;
class Display;
class Backlight;

namespace lvgl_ui {

/**
 * @brief Экран настроек звука, микрофона и экрана на базе нативных виджетов LVGL 9
 * (lv_button, lv_slider, lv_label).
 */
class LvglAudioSettings {
public:
    static LvglAudioSettings& GetInstance();

    void Initialize(Display* display, Es8311AudioCodec* codec, Backlight* backlight = nullptr);
    void Open();
    void Close();
    void Toggle();
    bool IsOpen() const { return modal_obj_ != nullptr; }

private:
    LvglAudioSettings() = default;
    ~LvglAudioSettings();

    LvglAudioSettings(const LvglAudioSettings&) = delete;
    LvglAudioSettings& operator=(const LvglAudioSettings&) = delete;

    struct SettingRow {
        std::string name;
        int min_val = 0;
        int max_val = 100;
        int current_val = 0;
        int step = 5;
        std::string unit;
        lv_obj_t* label_val = nullptr;
        lv_obj_t* slider = nullptr;
        std::function<void(int)> on_change;
    };

    void BuildUI();
    void DestroyUI();
    void CreateEntryChip();
    void CreateRow(lv_obj_t* parent, SettingRow& row);
    void UpdateRowValue(SettingRow& row, int val);

    Display* display_ = nullptr;
    Es8311AudioCodec* codec_ = nullptr;
    Backlight* backlight_ = nullptr;
    lv_obj_t* modal_obj_ = nullptr;
    lv_obj_t* entry_chip_ = nullptr;

    SettingRow row_volume_;
    SettingRow row_mic_gain_;
    SettingRow row_brightness_;
    SettingRow row_echo_delay_;
    SettingRow row_echo_tolerance_;

    // Указатели виджетов строк живут в самих SettingRow, поэтому после
    // DestroyUI их надо обнулить — иначе следующий Open() обновляет уже
    // удалённые объекты (lv_obj_delete_async выполняется в следующем тике).
    void ForgetRowWidgets();
    std::vector<SettingRow*> rows_;
};

}  // namespace lvgl_ui
