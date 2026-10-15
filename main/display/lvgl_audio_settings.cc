#include "lvgl_audio_settings.h"

#include <esp_log.h>
#include <cstdio>

#include <algorithm>

#include "audio/codecs/es8311_audio_codec.h"
#include "board.h"
#include "display/display.h"

#define TAG "LvglAudioSettings"

// Кириллица: дефолтный шрифт LVGL в этой сборке — montserrat_14
// (CONFIG_LV_FONT_DEFAULT), русских литер в нём нет, а
// CONFIG_LV_USE_FONT_PLACEHOLDER=n, поэтому подпись без явно заданного шрифта
// не рисуется вообще (пустые кнопки). Берём тот же встроенный Noto, что и чат:
// для GUITION это font_noto_sans_basic_30_4 с полным русским подмножеством.
// Стиль задаётся только контейнерам — дети наследуют text_font по родителям.
LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

namespace lvgl_ui {

LvglAudioSettings& LvglAudioSettings::GetInstance() {
    static LvglAudioSettings instance;
    return instance;
}

LvglAudioSettings::~LvglAudioSettings() {
    Close();
    if (entry_chip_ != nullptr) {
        lv_obj_delete_async(entry_chip_);
        entry_chip_ = nullptr;
    }
}

void LvglAudioSettings::Initialize(Display* display, Es8311AudioCodec* codec, Backlight* backlight) {
    display_ = display;
    codec_ = codec;
    backlight_ = backlight;
    if (display_ != nullptr) {
        DisplayLockGuard lock(display_);
        CreateEntryChip();
        // Сразу открываем экран настроек, чтобы он был виден на дисплее
        BuildUI();
    }
}

void LvglAudioSettings::CreateEntryChip() {
    if (entry_chip_ != nullptr || display_ == nullptr) {
        return;
    }

    lv_obj_t* top = lv_layer_top();
    entry_chip_ = lv_button_create(top);
    lv_obj_set_size(entry_chip_, 230, 52);
    lv_obj_set_pos(entry_chip_, 24, 524);
    lv_obj_set_style_bg_color(entry_chip_, lv_color_hex(0x2E3440), 0);
    lv_obj_set_style_bg_color(entry_chip_, lv_color_hex(0x4C566A), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(entry_chip_, 2, 0);
    lv_obj_set_style_border_color(entry_chip_, lv_color_hex(0x88C0D0), 0);
    lv_obj_set_style_radius(entry_chip_, 16, 0);
    lv_obj_set_style_shadow_width(entry_chip_, 20, 0);
    lv_obj_set_style_shadow_color(entry_chip_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(entry_chip_, LV_OPA_60, 0);
    // Один раз на контейнер — все дочерние метки получают кириллический шрифт.
    lv_obj_set_style_text_font(entry_chip_, &BUILTIN_TEXT_FONT, 0);

    lv_obj_t* lbl = lv_label_create(entry_chip_);
    lv_label_set_text(lbl, "Настройки звука");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x88C0D0), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(
        entry_chip_,
        [](lv_event_t* e) {
            LvglAudioSettings::GetInstance().Open();
        },
        LV_EVENT_CLICKED, nullptr);
}

void LvglAudioSettings::Open() {
    if (modal_obj_ != nullptr || display_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(display_);
    BuildUI();
}

void LvglAudioSettings::Close() {
    if (modal_obj_ == nullptr || display_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(display_);
    DestroyUI();
}

void LvglAudioSettings::Toggle() {
    if (IsOpen()) {
        Close();
    } else {
        Open();
    }
}

void LvglAudioSettings::UpdateRowValue(SettingRow& row, int val) {
    row.current_val = val;
    if (row.slider != nullptr && lv_slider_get_value(row.slider) != val) {
        lv_slider_set_value(row.slider, val, LV_ANIM_OFF);
    }
    if (row.label_val != nullptr) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%s", val, row.unit.c_str());
        lv_label_set_text(row.label_val, buf);
    }
}

void LvglAudioSettings::CreateRow(lv_obj_t* parent, SettingRow& row) {
    // Строка участвует в «забыть виджеты» при закрытии экрана; реестр не
    // должен обрастать дублями при повторном Open().
    if (std::find(rows_.begin(), rows_.end(), &row) == rows_.end()) {
        rows_.push_back(&row);
    }

    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2E3440), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3B4252), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    // Верхняя строка карточки: название и значение
    lv_obj_t* header_row = lv_obj_create(card);
    lv_obj_set_width(header_row, lv_pct(100));
    lv_obj_set_height(header_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_row, 0, 0);
    lv_obj_set_style_pad_all(header_row, 0, 0);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl_title = lv_label_create(header_row);
    lv_label_set_text(lbl_title, row.name.c_str());
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xECEFF4), 0);

    row.label_val = lv_label_create(header_row);
    lv_obj_set_style_text_color(row.label_val, lv_color_hex(0xEBCB8B), 0);

    // Нижняя строка: кнопка [-], слайдер lv_slider, кнопка [+]
    lv_obj_t* ctrl_row = lv_obj_create(card);
    lv_obj_set_width(ctrl_row, lv_pct(100));
    lv_obj_set_height(ctrl_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_row, 12, 0);

    // Нативная кнопка LVGL 9: lv_button [- шаг]
    lv_obj_t* btn_minus = lv_button_create(ctrl_row);
    lv_obj_set_size(btn_minus, 64, 44);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x434C5E), 0);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x5E81AC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_minus, 8, 0);
    lv_obj_t* lbl_m = lv_label_create(btn_minus);
    char buf_m[16];
    snprintf(buf_m, sizeof(buf_m), "-%d", row.step);
    lv_label_set_text(lbl_m, buf_m);
    lv_obj_center(lbl_m);

    // Нативный слайдер LVGL 9: lv_slider
    row.slider = lv_slider_create(ctrl_row);
    lv_obj_set_flex_grow(row.slider, 1);
    lv_obj_set_height(row.slider, 24);
    lv_slider_set_range(row.slider, row.min_val, row.max_val);
    lv_slider_set_value(row.slider, row.current_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(row.slider, lv_color_hex(0x3B4252), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row.slider, lv_color_hex(0x88C0D0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(row.slider, lv_color_hex(0xECEFF4), LV_PART_KNOB);
    lv_obj_set_style_radius(row.slider, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(row.slider, 12, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(row.slider, 5, LV_PART_KNOB);

    // Нативная кнопка LVGL 9: lv_button [+ шаг]
    lv_obj_t* btn_plus = lv_button_create(ctrl_row);
    lv_obj_set_size(btn_plus, 64, 44);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x434C5E), 0);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x5E81AC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_plus, 8, 0);
    lv_obj_t* lbl_p = lv_label_create(btn_plus);
    char buf_p[16];
    snprintf(buf_p, sizeof(buf_p), "+%d", row.step);
    lv_label_set_text(lbl_p, buf_p);
    lv_obj_center(lbl_p);

    // Подпись шкалы под слайд-баром (Мин / Шаг / Макс)
    lv_obj_t* scale_row = lv_obj_create(card);
    lv_obj_set_width(scale_row, lv_pct(100));
    lv_obj_set_height(scale_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(scale_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scale_row, 0, 0);
    lv_obj_set_style_pad_all(scale_row, 0, 0);
    lv_obj_set_flex_flow(scale_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scale_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    char min_str[32];
    snprintf(min_str, sizeof(min_str), "Мин: %d%s", row.min_val, row.unit.c_str());
    lv_obj_t* lbl_min = lv_label_create(scale_row);
    lv_label_set_text(lbl_min, min_str);
    lv_obj_set_style_text_color(lbl_min, lv_color_hex(0x98A4B8), 0);

    // Встроенный шрифт 30 px, поэтому средняя подпись короткая: длинная строка
    // выталкивает «Мин»/«Макс» за карточку.
    char step_str[32];
    snprintf(step_str, sizeof(step_str), "шаг ±%d", row.step);
    lv_obj_t* lbl_step = lv_label_create(scale_row);
    lv_label_set_text(lbl_step, step_str);
    lv_obj_set_style_text_color(lbl_step, lv_color_hex(0x6A778D), 0);

    char max_str[32];
    snprintf(max_str, sizeof(max_str), "Макс: %d%s", row.max_val, row.unit.c_str());
    lv_obj_t* lbl_max = lv_label_create(scale_row);
    lv_label_set_text(lbl_max, max_str);
    lv_obj_set_style_text_color(lbl_max, lv_color_hex(0x98A4B8), 0);

    // Обновляем метку начальным значением
    UpdateRowValue(row, row.current_val);

    // Событие слайдера
    lv_obj_add_event_cb(
        row.slider,
        [](lv_event_t* e) {
            auto* r = static_cast<SettingRow*>(lv_event_get_user_data(e));
            int val = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)));
            r->current_val = val;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d%s", val, r->unit.c_str());
            lv_label_set_text(r->label_val, buf);
            if (r->on_change) {
                r->on_change(val);
            }
        },
        LV_EVENT_VALUE_CHANGED, &row);

    // Событие кнопки [- шаг]
    lv_obj_add_event_cb(
        btn_minus,
        [](lv_event_t* e) {
            auto* r = static_cast<SettingRow*>(lv_event_get_user_data(e));
            r->current_val -= r->step;
            if (r->current_val < r->min_val) {
                r->current_val = r->min_val;
            }
            lv_slider_set_value(r->slider, r->current_val, LV_ANIM_OFF);
            char buf[32];
            snprintf(buf, sizeof(buf), "%d%s", r->current_val, r->unit.c_str());
            lv_label_set_text(r->label_val, buf);
            if (r->on_change) {
                r->on_change(r->current_val);
            }
        },
        LV_EVENT_CLICKED, &row);

    // Событие кнопки [+ шаг]
    lv_obj_add_event_cb(
        btn_plus,
        [](lv_event_t* e) {
            auto* r = static_cast<SettingRow*>(lv_event_get_user_data(e));
            r->current_val += r->step;
            if (r->current_val > r->max_val) {
                r->current_val = r->max_val;
            }
            lv_slider_set_value(r->slider, r->current_val, LV_ANIM_OFF);
            char buf[32];
            snprintf(buf, sizeof(buf), "%d%s", r->current_val, r->unit.c_str());
            lv_label_set_text(r->label_val, buf);
            if (r->on_change) {
                r->on_change(r->current_val);
            }
        },
        LV_EVENT_CLICKED, &row);
}

void LvglAudioSettings::BuildUI() {
    lv_obj_t* parent = lv_layer_top();

    if (entry_chip_ != nullptr) {
        lv_obj_add_flag(entry_chip_, LV_OBJ_FLAG_HIDDEN);
    }

    // Модальный контейнер (диалог)
    modal_obj_ = lv_obj_create(parent);
    lv_obj_set_size(modal_obj_, 860, 530);
    lv_obj_center(modal_obj_);
    lv_obj_set_style_bg_color(modal_obj_, lv_color_hex(0x1F232A), 0);
    lv_obj_set_style_bg_opa(modal_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(modal_obj_, 2, 0);
    lv_obj_set_style_border_color(modal_obj_, lv_color_hex(0x4C566A), 0);
    lv_obj_set_style_radius(modal_obj_, 16, 0);
    lv_obj_set_style_shadow_width(modal_obj_, 40, 0);
    lv_obj_set_style_shadow_color(modal_obj_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(modal_obj_, 18, 0);
    // Кириллический шрифт наследуется всеми карточками, кнопками и подписями.
    lv_obj_set_style_text_font(modal_obj_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_flex_flow(modal_obj_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(modal_obj_, 12, 0);

    // Заголовок и кнопка закрытия
    lv_obj_t* header = lv_obj_create(modal_obj_);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title_label = lv_label_create(header);
    lv_label_set_text(title_label, "Настройки звука и дисплея (LVGL 9)");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x88C0D0), 0);

    lv_obj_t* btn_close_top = lv_button_create(header);
    lv_obj_set_size(btn_close_top, 96, 42);
    lv_obj_set_style_bg_color(btn_close_top, lv_color_hex(0xBF616A), 0);
    lv_obj_set_style_bg_color(btn_close_top, lv_color_hex(0xD08770), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_close_top, 8, 0);
    lv_obj_t* lbl_x = lv_label_create(btn_close_top);
    lv_label_set_text(lbl_x, "Закрыть");
    lv_obj_center(lbl_x);
    lv_obj_add_event_cb(
        btn_close_top,
        [](lv_event_t* e) {
            LvglAudioSettings::GetInstance().Close();
        },
        LV_EVENT_CLICKED, nullptr);

    // Контейнер скролла со строками настроек
    lv_obj_t* scroll_content = lv_obj_create(modal_obj_);
    lv_obj_set_width(scroll_content, lv_pct(100));
    lv_obj_set_flex_grow(scroll_content, 1);
    lv_obj_set_style_bg_opa(scroll_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_content, 0, 0);
    lv_obj_set_style_pad_all(scroll_content, 0, 0);
    lv_obj_set_flex_flow(scroll_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll_content, 10, 0);

    // 1. Громкость динамика
    row_volume_.name = "Громкость динамика";
    row_volume_.min_val = 0;
    row_volume_.max_val = 100;
    row_volume_.step = 5;
    row_volume_.unit = "%";
    row_volume_.current_val = codec_ ? codec_->output_volume() : 70;
    row_volume_.on_change = [this](int v) {
        if (codec_) {
            codec_->SetOutputVolume(v);
        }
    };
    CreateRow(scroll_content, row_volume_);

    // 2. Усиление микрофона. esp_codec_dev_set_in_gain() ждёт децибелы, а не
    // проценты: ES8311 умеет 0..42 дБ шагами по 6 дБ, практический верх,
    // проверенный на этой плате, — 30 дБ. Раньше сюда приходило 0..100, и
    // любое значение выше 42 превращалось в ошибку ctrl_if (шум + срыв усиления).
    row_mic_gain_.name = "Усиление микрофона";
    row_mic_gain_.min_val = 0;
    row_mic_gain_.max_val = 30;
    row_mic_gain_.step = 1;
    row_mic_gain_.unit = " дБ";
    row_mic_gain_.current_val = codec_ ? static_cast<int>(codec_->input_gain()) : 30;
    row_mic_gain_.on_change = [this](int v) {
        if (codec_) {
            codec_->SetInputGain(static_cast<float>(v));
        }
    };
    CreateRow(scroll_content, row_mic_gain_);

    // 3. Яркость дисплея
    row_brightness_.name = "Яркость подсветки";
    row_brightness_.min_val = 5;
    row_brightness_.max_val = 100;
    row_brightness_.step = 5;
    row_brightness_.unit = "%";
    row_brightness_.current_val = backlight_ ? backlight_->brightness() : 80;
    row_brightness_.on_change = [this](int v) {
        if (backlight_) {
            backlight_->SetBrightness(v);
        }
    };
    CreateRow(scroll_content, row_brightness_);

    // 4. Задержка эхоподавления (AEC delay)
    row_echo_delay_.name = "Задержка эхоподавления AEC";
    row_echo_delay_.min_val = 10;
    row_echo_delay_.max_val = 250;
    row_echo_delay_.step = 5;
    row_echo_delay_.unit = " мс";
    row_echo_delay_.current_val = codec_ ? codec_->EchoDelayMs() : 76;
    row_echo_delay_.on_change = [this](int v) {
        if (codec_) {
            codec_->SetEchoDelayMs(v);
        }
    };
    CreateRow(scroll_content, row_echo_delay_);

    // 5. Допуск рассинхрона опорного канала — тот же параметр, что на экране
    // «Настройка эха» в EmoteDisplay: насколько свободно опорному сигналу
    // «гулять» вокруг замеренной задержки (перебуферизация DMA, паузы).
    row_echo_tolerance_.name = "Допуск рассинхрона AEC";
    row_echo_tolerance_.min_val = 10;
    row_echo_tolerance_.max_val = 100;
    row_echo_tolerance_.step = 2;
    row_echo_tolerance_.unit = " мс";
    row_echo_tolerance_.current_val = codec_ ? codec_->RefToleranceMs() : 40;
    row_echo_tolerance_.on_change = [this](int v) {
        if (codec_) {
            codec_->SetRefToleranceMs(v);
        }
    };
    CreateRow(scroll_content, row_echo_tolerance_);

    // Нижняя панель действий (кнопки lv_button)
    lv_obj_t* footer = lv_obj_create(modal_obj_);
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // Кнопка «Автопоиск эха»: включает коррелятор, который сам найдёт пик эха
    // и зафиксирует задержку (снимает ручную настройку row_echo_delay_).
    lv_obj_t* btn_scan = lv_button_create(footer);
    lv_obj_set_size(btn_scan, 150, 42);
    lv_obj_set_style_bg_color(btn_scan, lv_color_hex(0x88C0D0), 0);
    lv_obj_set_style_bg_color(btn_scan, lv_color_hex(0x8FBCBB), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_scan, 8, 0);
    lv_obj_t* lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "Автопоиск эха");
    lv_obj_center(lbl_scan);
    lv_obj_set_style_text_color(lbl_scan, lv_color_hex(0x1F232A), 0);
    lv_obj_add_event_cb(
        btn_scan,
        [](lv_event_t* e) {
            auto* self = &LvglAudioSettings::GetInstance();
            if (self->codec_) {
                self->codec_->RestartEchoScan();
            }
            ESP_LOGI(TAG, "Автопоиск задержки эха перезапущен");
        },
        LV_EVENT_CLICKED, nullptr);

    // Кнопка «Сохранить»
    lv_obj_t* btn_save = lv_button_create(footer);
    lv_obj_set_size(btn_save, 140, 42);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0xA3BE8C), 0);
    lv_obj_set_style_radius(btn_save, 8, 0);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Сохранить");
    lv_obj_center(lbl_save);
    lv_obj_add_event_cb(
        btn_save,
        [](lv_event_t* e) {
            auto* self = &LvglAudioSettings::GetInstance();
            if (self->codec_) {
                self->codec_->SaveEchoTuning();
            }
            ESP_LOGI(TAG, "Настройки сохранены в NVS");
        },
        LV_EVENT_CLICKED, nullptr);

    // Кнопка «Диагностика»
    lv_obj_t* btn_diag = lv_button_create(footer);
    lv_obj_set_size(btn_diag, 150, 42);
    lv_obj_set_style_bg_color(btn_diag, lv_color_hex(0x5E81AC), 0);
    lv_obj_set_style_radius(btn_diag, 8, 0);
    lv_obj_t* lbl_diag = lv_label_create(btn_diag);
    lv_label_set_text(lbl_diag, "Диагностика");
    lv_obj_center(lbl_diag);
    lv_obj_add_event_cb(
        btn_diag,
        [](lv_event_t* e) {
            auto* self = &LvglAudioSettings::GetInstance();
            if (self->codec_) {
                self->codec_->LogDiagnostics();
                self->codec_->LogEchoAlignment();
            }
        },
        LV_EVENT_CLICKED, nullptr);

    // Кнопка «Закрыть»
    lv_obj_t* btn_close = lv_button_create(footer);
    lv_obj_set_size(btn_close, 140, 42);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x4C566A), 0);
    lv_obj_set_style_radius(btn_close, 8, 0);
    lv_obj_t* lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Закрыть");
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(
        btn_close,
        [](lv_event_t* e) {
            LvglAudioSettings::GetInstance().Close();
        },
        LV_EVENT_CLICKED, nullptr);
}

void LvglAudioSettings::ForgetRowWidgets() {
    for (SettingRow* r : rows_) {
        r->slider = nullptr;
        r->label_val = nullptr;
    }
}

void LvglAudioSettings::DestroyUI() {
    if (modal_obj_ != nullptr) {
        lv_obj_delete_async(modal_obj_);
        modal_obj_ = nullptr;
    }
    // Удаление отложено на следующий тик LVGL, но указатели строк становятся
    // мусором сразу — обнуляем их до того, как кто-нибудь дёрнет UpdateRowValue.
    ForgetRowWidgets();
    if (entry_chip_ != nullptr) {
        lv_obj_remove_flag(entry_chip_, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace lvgl_ui
