#include "emote_audio_settings.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <cstdio>
#include <cstring>

#include "audio/codecs/es8311_audio_codec.h"
#include "emote_display.h"
#include "gfx.h"

#define TAG "EmoteAudioUI"

namespace emote {

namespace {

// ===== Геометрия под 1024x600 и шрифт 20 px =====
constexpr int kRowTop = 96;       // первая строка параметров
constexpr int kRowPitch = 82;     // высота строки вместе с отступом
constexpr int kControlH = 40;
constexpr int kNameX = 40;
constexpr int kNameW = 360;
// Пояснение «за что отвечает параметр» — справа от названия, в пустой полосе
// между названием и числом (число начинается с kValueX=896).
constexpr int kHintX = 412;
constexpr int kHintW = 470;
constexpr int kMinusX = 40;
constexpr int kStepW = 64;
constexpr int kTrackX = 112;
constexpr int kTrackW = 698;
constexpr int kPlusX = 820;
constexpr int kValueX = 896;
constexpr int kValueW = 100;
constexpr int kButtonY = 436;
constexpr int kStatusY = 500;

// Палитра: тёмный фон + читаемый светлый текст, accents — синий для заполнения
// слайдера и янтарный для чисел.
constexpr uint32_t kColorText = 0xECEFF4;
constexpr uint32_t kColorHint = 0x9AA5B4;
constexpr uint32_t kColorValue = 0xFFD37A;
constexpr uint32_t kColorTrack = 0x2A2F3A;
constexpr uint32_t kColorFill = 0x4A9EFF;
constexpr uint32_t kColorButton = 0x39404E;
constexpr uint32_t kColorButtonActive = 0x5A6577;

constexpr int64_t kRepeatDelayMs = 350;  // задержка до автоповтора «-»/«+»
constexpr int64_t kRepeatPeriodMs = 130;
constexpr int64_t kApplyThrottleMs = 100;  // применение значения при перетаскивании
// Ждём, пока приложение смонтирует ассеты (оттуда берётся шрифт с кириллицей),
// но не вечно: после дедлайна собираем экран на встроенном шрифте.
constexpr int64_t kFontWaitMs = 90000;

// ===== Регистры GT911 =====
// Схема чтения повторяет проверенный на этой плате эталон (CamBrowser,
// main/camos/touch.c): отчёт о касании лежит в 0x814E (бит 7 — «есть новые
// данные», младшие биты — число точек), координаты первой точки начинаются с
// 0x814F, 8 байт на точку: track_id, Xlo, Xhi, Ylo, Yhi, Sizelo, Sizehi, rsvd.
// После чтения статус обязан быть обнулён, иначе чип больше ничего не пришлёт.
constexpr uint16_t kGt911StatusReg = 0x814E;
constexpr uint16_t kGt911PointReg = 0x814F;
constexpr uint16_t kGt911ConfigReg = 0x8047;
constexpr int kGt911PointBytes = 8;
constexpr int kGt911MaxPoints = 5;
// Сколько времени считаем палец удержанным без новых отчётов чипа. При
// удержании GT911 генерирует отчёт каждые ~10 мс, так что запас в 120 мс
// покрывает и занятую ES8311 шину, и пропущенные опросы.
constexpr int64_t kHoldGraceMs = 120;
// Запас поля попадания: вертикальный больше, потому что строки стоят на шаге
// 82 px при высоте контрола 40 px; горизонтальный меньше, чтобы «-», дорожка и
// «+» в одной строке не перекрывались.
constexpr int kHitPadX = 4;
constexpr int kHitPadY = 8;

int64_t NowMs() {
    return esp_timer_get_time() / 1000;
}

int Clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ===== Доступ к параметрам кодека через функции-адаптеры =====
// Слайдеру всё равно, что он крутит: ему нужна пара «прочитать/применить» в
// целых числах. Хранение, клампинг и запись в железо остаются в кодеке.
int GetEchoDelay(Es8311AudioCodec* c) {
    return c->EchoDelayMs();
}
void SetEchoDelay(Es8311AudioCodec* c, int v) {
    c->SetEchoDelayMs(v);
}
int GetTolerance(Es8311AudioCodec* c) {
    return c->RefToleranceMs();
}
void SetTolerance(Es8311AudioCodec* c, int v) {
    c->SetRefToleranceMs(v);
}
int GetVolume(Es8311AudioCodec* c) {
    return c->output_volume();
}
void SetVolume(Es8311AudioCodec* c, int v) {
    c->SetOutputVolume(v);
}
int GetGain(Es8311AudioCodec* c) {
    return static_cast<int>(c->input_gain());
}
void SetGain(Es8311AudioCodec* c, int v) {
    c->SetInputGain(static_cast<float>(v));
}

}  // namespace

EmoteAudioSettings::EmoteAudioSettings(EmoteDisplay* display, esp_lcd_panel_io_handle_t touch_io,
                                       Es8311AudioCodec* codec)
    : emote_(display != nullptr ? display->GetEmoteHandle() : nullptr),
      display_(display),
      touch_io_(touch_io),
      codec_(codec) {
    sliders_.resize(4);
    // title — что это, hint — одна строка «за что отвечает» прямо в строке,
    // detail — развёрнутая формулировка в нижней строке, когда палец на этом
    // параметре. Без hint/detail экран выглядит как четыре полоски с цифрами.
    // Длины подобраны так, чтобы текст влез в своё поле без обрезки: hint — до
    // ~40 знаков (поле 470 px), detail вместе с title и числом — до ~70 (944 px).
    sliders_[0] = {.title = "Задержка эха",
                   .hint = "когда помощник слышит свой динамик",
                   .detail = "пауза, через которую колонка долетает до микрофона",
                   .unit = "мс",
                   .min_value = 40,
                   .max_value = 140,
                   .step = 2,
                   .getter = GetEchoDelay,
                   .setter = SetEchoDelay};
    sliders_[1] = {.title = "Допуск рассинхрона",
                   .hint = "насколько задержка может «плавать»",
                   .detail = "запас вокруг задержки: больше — терпит дрейф",
                   .unit = "мс",
                   .min_value = 10,
                   .max_value = 100,
                   .step = 2,
                   .getter = GetTolerance,
                   .setter = SetTolerance};
    sliders_[2] = {.title = "Громкость динамика",
                   .hint = "громче динамик — сильнее эхо",
                   .detail = "то, что попадает в микрофон от самого себя",
                   .unit = "%",
                   .min_value = 0,
                   .max_value = 100,
                   .step = 1,
                   .getter = GetVolume,
                   .setter = SetVolume};
    sliders_[3] = {.title = "Усиление микрофона",
                   .hint = "выше = слышны шумы и эхо",
                   .detail = "насколько сильно усиливается сигнал с микрофона",
                   .unit = "дБ",
                   .min_value = 0,
                   .max_value = 30,
                   .step = 1,
                   .getter = GetGain,
                   .setter = SetGain};

    deadline_ms_ = NowMs() + kFontWaitMs;
    // Ориентация и масштаб координат тача берутся из конфига самого GT911:
    // панель EmoteDisplay рисуется 1:1 в 1024x600, поэтому тапы должны
    // попадать в эту же систему координат без «LVGL-ных» зеркалений.
    ProbeGt911Config();
    xTaskCreate(TaskEntry, "aud_touch", 6144, this, 3, &task_);
    ESP_LOGI(TAG, "Экран настроек звука: задача опроса тача запущена");
}

EmoteAudioSettings::~EmoteAudioSettings() {
    stop_task_ = true;
    for (int i = 0; i < 20 && task_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    DisposeUI();
    if (font_copy_ != nullptr) {
        free(font_copy_);
        font_copy_ = nullptr;
    }
}

void EmoteAudioSettings::TaskEntry(void* arg) {
    auto* self = static_cast<EmoteAudioSettings*>(arg);
    while (!self->stop_task_) {
        self->PollLoop();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    self->task_ = nullptr;
    vTaskDelete(NULL);
}

bool EmoteAudioSettings::LoadCyrillicFont() {
    // Название шрифта не хардкодим: берём из index.json того же ассет-раздела,
    // откуда emote грузит анимации глаз. Так экран переживёт смену сборки
    // ассетов (другое разрешение/другой набор файлов).
    const uint8_t* json_data = nullptr;
    size_t json_size = 0;
    if (emote_get_asset_data_by_name(emote_, "index.json", &json_data, &json_size) != ESP_OK ||
        json_data == nullptr || json_size == 0) {
        return false;  // ассеты ещё не смонтированы — попробуем в следующем тике
    }
    // Данные из mmap не гарантированно оканчиваются нулём — копируем перед
    // разбором.
    auto* json_copy = static_cast<char*>(malloc(json_size + 1));
    if (json_copy == nullptr) {
        return false;
    }
    memcpy(json_copy, json_data, json_size);
    json_copy[json_size] = '\0';
    cJSON* root = cJSON_Parse(json_copy);
    free(json_copy);
    if (root == nullptr) {
        ESP_LOGW(TAG, "index.json не разобрался");
        return false;
    }
    cJSON* font_item = cJSON_GetObjectItem(root, "text_font");
    char* font_name = cJSON_IsString(font_item) ? font_item->valuestring : nullptr;
    const uint8_t* font_data = nullptr;
    size_t font_size = 0;
    esp_err_t err = ESP_FAIL;
    if (font_name != nullptr) {
        err = emote_get_asset_data_by_name(emote_, font_name, &font_data, &font_size);
    }
    cJSON_Delete(root);
    if (err != ESP_OK || font_data == nullptr || font_size < sizeof(lv_font_t)) {
        ESP_LOGW(TAG, "шрифт из index.json не найден (0x%x)", err);
        return false;
    }
    // gfx_font_lv_load_from_binary НЕ копирует глифы: он лишь пересобирает
    // указатели внутри переданного буфера, поэтому буфер обязан жить всё время
    // работы экрана. 1.2 МБ держим в PSRAM: внутренняя память нужна AFE.
    font_copy_ = static_cast<uint8_t*>(
        heap_caps_malloc(font_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (font_copy_ == nullptr) {
        font_copy_ = static_cast<uint8_t*>(malloc(font_size));
    }
    if (font_copy_ == nullptr) {
        ESP_LOGE(TAG, "нет памяти под копию шрифта (%d байт)", (int)font_size);
        return false;
    }
    memcpy(font_copy_, font_data, font_size);
    font_ = gfx_font_lv_load_from_binary(font_copy_);
    if (font_ == nullptr) {
        ESP_LOGE(TAG, "шрифт не загрузился");
        free(font_copy_);
        font_copy_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Шрифт экрана загружен (%d байт)", (int)font_size);
    // Проверка покрытия: «загрузился» ещё не значит «в нём есть русские буквы».
    // Считаем, сколько знаков U+0410..U+044F шрифт реально умеет, и отдельно
    // проверяем знаки, которыми набраны подписи экрана: « » · — … и Ё ё. Если
    // знака нет, LVGL рисует на его месте либо ничего, либо квадрат — подпись
    // читается как «не по-русски», хотя название параметра целое.
    static const uint32_t kProbe[] = {0x00AB, 0x00BB, 0x00B7, 0x2014, 0x2013, 0x2026,
                                      0x0401, 0x0451, 0x0406, 0x0456, 0x0491, 0x0492};
    char missing[96];
    missing[0] = '\0';
    size_t missing_len = 0;
    for (uint32_t cp : kProbe) {
        lv_font_glyph_dsc_t gd;
        if (!lv_font_get_glyph_dsc(font_, &gd, cp, 0) && missing_len + 8 < sizeof(missing)) {
            missing_len += snprintf(missing + missing_len, sizeof(missing) - missing_len,
                                    " U+%04X", (unsigned)cp);
        }
    }
    int cyrillic = 0;
    char missing_letters[80];
    missing_letters[0] = '\0';
    size_t letters_len = 0;
    for (uint32_t cp = 0x0410; cp <= 0x044F; ++cp) {
        lv_font_glyph_dsc_t gd;
        if (lv_font_get_glyph_dsc(font_, &gd, cp, 0)) {
            ++cyrillic;
        } else if (letters_len + 8 < sizeof(missing_letters)) {
            letters_len += snprintf(missing_letters + letters_len,
                                    sizeof(missing_letters) - letters_len, " U+%04X",
                                    (unsigned)cp);
        }
    }
    lv_font_glyph_dsc_t latin_dsc;
    const bool has_latin = lv_font_get_glyph_dsc(font_, &latin_dsc, 'A', 0);
    ESP_LOGW(TAG,
             "Покрытие шрифта: кириллица %d/67%s, латиница %s, нет знаков:%s", cyrillic,
             missing_letters, has_latin ? "есть" : "НЕТ", missing[0] ? missing : " (все есть)");
    if (cyrillic == 0) {
        ESP_LOGE(TAG,
                 "В этом шрифте нет русских букв — подписи экрана будут пустыми. Нужен cbin из "
                 "сборки с кириллицей (см. навык lvgl-cyrillic-fonts).");
    }
    return true;
}

gfx_obj_t* EmoteAudioSettings::MakeLabel(const char* obj_name, const char* text, int x, int y,
                                         int w, int h, uint32_t color, uint32_t bg, bool bg_enable,
                                         gfx_text_align_t align) {
    gfx_obj_t* obj = emote_create_obj_by_type(emote_, EMOTE_OBJ_TYPE_LABEL, obj_name);
    if (obj == nullptr) {
        ESP_LOGE(TAG, "не удалось создать label %s", obj_name);
        return nullptr;
    }
    // Конфигуратор expression ставит центрирование, «бегущую строку» и
    // встроенный шрифт без кириллицы — перекрываем всё нужным.
    gfx_label_set_long_mode(obj, GFX_LABEL_LONG_CLIP);
    if (font_ != nullptr) {
        gfx_label_set_font(obj, static_cast<gfx_font_t>(font_));
    }
    gfx_label_set_text(obj, text);
    gfx_label_set_color(obj, GFX_COLOR_HEX(color));
    gfx_label_set_text_align(obj, align);
    gfx_label_set_bg_enable(obj, bg_enable);
    gfx_label_set_bg_color(obj, GFX_COLOR_HEX(bg));
    gfx_obj_set_size(obj, (uint16_t)w, (uint16_t)h);
    gfx_obj_set_pos(obj, (gfx_coord_t)x, (gfx_coord_t)y);
    gfx_obj_set_visible(obj, true);
    return obj;
}

void EmoteAudioSettings::AddRegion(int x, int y, int w, int h, Kind kind, int row, int action) {
    Region r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r.kind = kind;
    r.row = row;
    r.action = action;
    regions_.push_back(r);
}

bool EmoteAudioSettings::BuildUI() {
    if (emote_ == nullptr || codec_ == nullptr) {
        return false;
    }
    char name[32];

    // Плашка входа живёт и в обычном режиме — это основной способ попасть в
    // экран (долгое нажатие BOOT остаётся запасным).
    entry_ = MakeLabel("aud_entry", "Настройки звука", 24, 552, 250, 38, kColorText, kColorButton,
                       true, GFX_TEXT_ALIGN_CENTER);
    if (entry_ == nullptr) {
        return false;
    }
    // Зона входа шире самой плашки: до нижнего края экрана, потому что по
    // такой плашке попадают пальцем именно вниз, а 38 px высоты на это не
    // оставляют запаса.
    AddRegion(16, 546, 266, 54, Kind::kEntry);

    title_ = MakeLabel("aud_title", "Настройка звука и эха", kNameX, 20, 944, 30, kColorText, 0,
                       false, GFX_TEXT_ALIGN_LEFT);
    hint_ = MakeLabel("aud_hint",
                      "Помощник слышит свой динамик. Подвигайте задержку, пока он не перестанет "
                      "отвечать самому себе.",
                      kNameX, 52, 944, 28, kColorHint, 0, false, GFX_TEXT_ALIGN_LEFT);

    for (int i = 0; i < 4; ++i) {
        Slider& s = sliders_[i];
        const int y = kRowTop + i * kRowPitch;
        snprintf(name, sizeof(name), "aud_s%d_name", i);
        s.name = MakeLabel(name, s.title, kNameX, y, kNameW, 26, kColorText, 0, false,
                           GFX_TEXT_ALIGN_LEFT);
        snprintf(name, sizeof(name), "aud_s%d_hint", i);
        s.row_hint = MakeLabel(name, s.hint, kHintX, y, kHintW, 26, kColorHint, 0, false,
                               GFX_TEXT_ALIGN_LEFT);
        snprintf(name, sizeof(name), "aud_s%d_value", i);
        s.value_label = MakeLabel(name, "0", kValueX, y, kValueW, 26, kColorValue, 0, false,
                                  GFX_TEXT_ALIGN_RIGHT);
        // На кнопках шага пишут сам шаг, а не голые «-»/«+»: рядом четыре
        // строки с разными шагами (2 мс, 2 мс, 1 %, 1 дБ), и по одному
        // знаку непонятно, насколько дёргается параметр.
        char step_minus[8];
        char step_plus[8];
        snprintf(step_minus, sizeof(step_minus), "-%d", s.step);
        snprintf(step_plus, sizeof(step_plus), "+%d", s.step);
        snprintf(name, sizeof(name), "aud_s%d_minus", i);
        s.minus = MakeLabel(name, step_minus, kMinusX, y + 34, kStepW, kControlH, kColorText,
                            kColorButton,
                            true, GFX_TEXT_ALIGN_CENTER);
        // Дорожка и заполнение — обычные label с включённым фоном: рисование
        // текста при пустой строке пропускается, остаётся ровно прямоугольник.
        snprintf(name, sizeof(name), "aud_s%d_track", i);
        s.track = MakeLabel(name, "", kTrackX, y + 34, kTrackW, kControlH, kColorText, kColorTrack,
                            true, GFX_TEXT_ALIGN_LEFT);
        snprintf(name, sizeof(name), "aud_s%d_fill", i);
        s.fill = MakeLabel(name, "", kTrackX, y + 34, 4, kControlH, kColorText, kColorFill, true,
                           GFX_TEXT_ALIGN_LEFT);
        snprintf(name, sizeof(name), "aud_s%d_plus", i);
        s.plus = MakeLabel(name, step_plus, kPlusX, y + 34, kStepW, kControlH, kColorText,
                           kColorButton,
                           true, GFX_TEXT_ALIGN_CENTER);
        if (s.name == nullptr || s.row_hint == nullptr || s.value_label == nullptr ||
            s.minus == nullptr || s.track == nullptr || s.fill == nullptr || s.plus == nullptr) {
            return false;
        }
        // Области касаний шире самих прямоугольников: пальцу по 7" экрану
        // нужно попадание ~40 px, а не пиксель в пиксель.
        AddRegion(kMinusX - 8, y + 30, kStepW + 16, kControlH + 12, Kind::kMinus, i);
        AddRegion(kTrackX, y + 30, kTrackW, kControlH + 12, Kind::kTrack, i);
        AddRegion(kPlusX - 8, y + 30, kStepW + 16, kControlH + 12, Kind::kPlus, i);
    }

    struct ButtonSpec {
        const char* obj;
        const char* text;
        int x;
        int w;
        int action;
    };
    const ButtonSpec specs[4] = {
        {"aud_b1", "Искать эхо", 40, 200, kActionRestart},
        {"aud_b2", "Сбросить", 252, 160, kActionReset},
        {"aud_b3", "Диагностика", 424, 190, kActionDiag},
        {"aud_b4", "Готово", 800, 144, kActionClose},
    };
    for (int i = 0; i < 4; ++i) {
        buttons_[i] = MakeLabel(specs[i].obj, specs[i].text, specs[i].x, kButtonY, specs[i].w, 50,
                                kColorText, kColorButton, true, GFX_TEXT_ALIGN_CENTER);
        if (buttons_[i] == nullptr) {
            return false;
        }
        AddRegion(specs[i].x - 6, kButtonY - 4, specs[i].w + 12, 58, Kind::kAction, -1,
                  specs[i].action);
    }

    status_ = MakeLabel("aud_status", "", kNameX, kStatusY, 944, 26, kColorHint, 0, false,
                        GFX_TEXT_ALIGN_LEFT);
    return status_ != nullptr;
}

bool EmoteAudioSettings::EnsureBuilt() {
    if (built_) {
        return ui_ok_;
    }
    if (emote_ == nullptr || codec_ == nullptr) {
        return false;
    }
    // Русские подписи требуют шрифт из ассетов, а приложение монтирует и
    // разбирает ассеты позже борда (Assets::Initialize). До этого момента
    // emote_get_asset_data_by_name() заведомо ошибается и печатает свою
    // ошибку — поэтому ждём флага от EmoteDisplay и не трогаем ассеты раньше.
    if (display_ != nullptr && !display_->AssetsLoaded()) {
        return false;
    }
    if (font_ == nullptr && NowMs() < deadline_ms_) {
        if (NowMs() - last_font_attempt_ms_ < 1000) {
            return false;
        }
        last_font_attempt_ms_ = NowMs();
        if (!LoadCyrillicFont()) {
            return false;
        }
    }
    if (font_ == nullptr) {
        ESP_LOGW(TAG, "кириллический шрифт так и не появился — экран соберётся без него");
    }
    emote_lock(emote_);
    const bool ok = BuildUI();
    if (ok) {
        // Собираем экран целиком скрытым: видна только плашка входа.
        for (Slider& s : sliders_) {
            gfx_obj_set_visible(s.name, false);
            gfx_obj_set_visible(s.row_hint, false);
            gfx_obj_set_visible(s.value_label, false);
            gfx_obj_set_visible(s.minus, false);
            gfx_obj_set_visible(s.plus, false);
            gfx_obj_set_visible(s.track, false);
            gfx_obj_set_visible(s.fill, false);
        }
        gfx_obj_set_visible(title_, false);
        gfx_obj_set_visible(hint_, false);
        gfx_obj_set_visible(status_, false);
        for (gfx_obj_t* b : buttons_) {
            gfx_obj_set_visible(b, false);
        }
        if (entry_ != nullptr) {
            gfx_obj_set_visible(entry_, true);
        }
        ESP_LOGI(TAG, "Экран настроек звука собран: областей %d", (int)regions_.size());
    }
    emote_unlock(emote_);
    built_ = true;
    ui_ok_ = ok;
    return ok;
}

void EmoteAudioSettings::DisposeUI() {
    // Объекты регистрирует emote (список custom objects), поэтому удаляем их
    // тоже через публичный gfx_obj_delete под замком менеджера.
    if (emote_ == nullptr || !built_) {
        return;
    }
    emote_lock(emote_);
    std::vector<gfx_obj_t*> all;
    all.push_back(entry_);
    all.push_back(title_);
    all.push_back(hint_);
    all.push_back(status_);
    for (gfx_obj_t* b : buttons_) {
        all.push_back(b);
    }
    for (Slider& s : sliders_) {
        all.push_back(s.name);
        all.push_back(s.row_hint);
        all.push_back(s.value_label);
        all.push_back(s.minus);
        all.push_back(s.plus);
        all.push_back(s.track);
        all.push_back(s.fill);
    }
    for (gfx_obj_t* obj : all) {
        if (obj != nullptr) {
            gfx_obj_delete(obj);
        }
    }
    emote_unlock(emote_);
    entry_ = nullptr;
    title_ = hint_ = status_ = nullptr;
    for (gfx_obj_t*& b : buttons_) {
        b = nullptr;
    }
    for (Slider& s : sliders_) {
        s.name = s.row_hint = s.value_label = s.minus = s.plus = s.track = s.fill = nullptr;
    }
    regions_.clear();
    built_ = false;
}

bool EmoteAudioSettings::CreateEntryChip() {
    return EnsureBuilt() && entry_ != nullptr;
}

void EmoteAudioSettings::Open() {
    if (open_ || !EnsureBuilt()) {
        return;
    }
    emote_lock(emote_);
    open_ = true;
    if (entry_ != nullptr) {
        gfx_obj_set_visible(entry_, false);
    }
    for (gfx_obj_t* obj : {title_, hint_, status_}) {
        if (obj != nullptr) {
            gfx_obj_set_visible(obj, true);
        }
    }
    for (gfx_obj_t* b : buttons_) {
        if (b != nullptr) {
            gfx_obj_set_visible(b, true);
        }
    }
    for (int i = 0; i < 4; ++i) {
        Slider& s = sliders_[i];
        s.value = Clamp(s.getter(codec_), s.min_value, s.max_value);
        gfx_obj_t* parts[7] = {s.name, s.row_hint, s.value_label, s.minus, s.track, s.fill,
                               s.plus};
        for (gfx_obj_t* p : parts) {
            if (p != nullptr) {
                gfx_obj_set_visible(p, true);
            }
        }
        UpdateSliderVisual(i);
    }
    // Лицо прячем: иначе глаза анимируются по центру экрана и мешают читать
    // подписи.
    emote_set_anim_visible(emote_, false);
    emote_unlock(emote_);
    UpdateStatusLine();
    ESP_LOGI(TAG, "Экран настроек звука открыт");
}

void EmoteAudioSettings::Close() {
    if (!open_) {
        return;
    }
    emote_lock(emote_);
    open_ = false;
    pressed_ = -1;
    if (entry_ != nullptr) {
        gfx_obj_set_visible(entry_, true);
    }
    for (gfx_obj_t* obj : {title_, hint_, status_}) {
        if (obj != nullptr) {
            gfx_obj_set_visible(obj, false);
        }
    }
    for (gfx_obj_t* b : buttons_) {
        if (b != nullptr) {
            gfx_obj_set_visible(b, false);
        }
    }
    for (Slider& s : sliders_) {
        gfx_obj_t* parts[7] = {s.name, s.row_hint, s.value_label, s.minus, s.track, s.fill,
                               s.plus};
        for (gfx_obj_t* p : parts) {
            if (p != nullptr) {
                gfx_obj_set_visible(p, false);
            }
        }
    }
    emote_set_anim_visible(emote_, true);
    emote_unlock(emote_);
    // Сохраняем уже после того, как отпустили замок графики: NVS пишет во flash.
    codec_->SaveEchoTuning();
    ESP_LOGI(TAG, "Экран настроек звука закрыт, значения сохранены");
}

bool EmoteAudioSettings::RegionContains(const Region& r, int x, int y) const {
    return x >= r.x - kHitPadX && x < r.x + r.w + kHitPadX && y >= r.y - kHitPadY &&
           y < r.y + r.h + kHitPadY;
}

int EmoteAudioSettings::HitRegion(int x, int y) const {
    // Идём с конца: последние добавленные области (кнопки) перекрывают дорожки
    // слайдеров, если когда-нибудь встанут в одну координату.
    for (int i = (int)regions_.size() - 1; i >= 0; --i) {
        const Region& r = regions_[i];
        // На закрытом экране активна только плашка входа, на открытом — только
        // её виджеты. Иначе тап по невидимой дорожке менял бы значение, которое
        // пользователь в этот момент не видит.
        if (open_ ? r.kind == Kind::kEntry : r.kind != Kind::kEntry) {
            continue;
        }
        if (RegionContains(r, x, y)) {
            return i;
        }
    }
    return -1;
}

void EmoteAudioSettings::UpdateSliderVisual(int row) {
    Slider& s = sliders_[row];
    const int span = s.max_value > s.min_value ? s.max_value - s.min_value : 1;
    const int filled = (s.value - s.min_value) * (kTrackW - 8) / span + 4;
    if (s.fill != nullptr) {
        gfx_obj_set_size(s.fill, (uint16_t)Clamp(filled, 4, kTrackW), kControlH);
    }
    if (s.value_label != nullptr) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d %s", s.value, s.unit);
        gfx_label_set_text_fmt(s.value_label, "%s", buf);
    }
}

void EmoteAudioSettings::ApplyRow(int row, bool throttle) {
    Slider& s = sliders_[row];
    if (s.setter == nullptr || codec_ == nullptr) {
        return;
    }
    const int64_t now = NowMs();
    // При перетаскивании дорожки значение меняется на каждом пикселе — кодек
    // применяем не чаще, чем раз в 100 мс, чтобы не топить шину I2C и лог.
    if (throttle && now - last_apply_ms_[row] < kApplyThrottleMs) {
        return;
    }
    last_apply_ms_[row] = now;
    s.setter(codec_, s.value);
}

void EmoteAudioSettings::SetValue(int row, int value, bool apply, bool throttle) {
    Slider& s = sliders_[row];
    const int clamped = Clamp(value, s.min_value, s.max_value);
    if (clamped == s.value) {
        return;
    }
    s.value = clamped;
    emote_lock(emote_);
    UpdateSliderVisual(row);
    emote_unlock(emote_);
    if (apply) {
        ApplyRow(row, throttle);
    }
}

void EmoteAudioSettings::StepValue(int row, int dir) {
    Slider& s = sliders_[row];
    SetValue(row, s.value + dir * s.step, true, false);
}

void EmoteAudioSettings::RunAction(int action) {
    switch (action) {
        case kActionRestart:
            codec_->RestartEchoScan();
            break;
        case kActionReset: {
            // Заводские значения: измеренная на этой плате задержка 76 мс и
            // допуск 50 мс, автопоиск снова включён.
            codec_->SetEchoDelayMs(76);
            codec_->SetRefToleranceMs(50);
            codec_->RestartEchoScan();
            break;
        }
        case kActionDiag:
            codec_->LogDiagnostics();
            codec_->LogEchoAlignment();
            break;
        case kActionClose:
            Close();
            return;
        default:
            return;
    }
    // Значики на экране обновляем с кодека, а не «на глаз»: после Reset/Restart
    // автокалибровка могла изменить задержку сама.
    emote_lock(emote_);
    for (int i = 0; i < 4; ++i) {
        Slider& s = sliders_[i];
        s.value = Clamp(s.getter(codec_), s.min_value, s.max_value);
        UpdateSliderVisual(i);
    }
    emote_unlock(emote_);
    UpdateStatusLine();
}

void EmoteAudioSettings::UpdateStatusLine() {
    if (status_ == nullptr) {
        return;
    }
    int measured_ms = 0;
    char buf[160];
    if (codec_->EchoDelayManual()) {
        snprintf(buf, sizeof(buf), "Ручные значения · задержка %d мс · допуск %d мс · сохранено",
                 codec_->EchoDelayMs(), codec_->RefToleranceMs());
    } else if (codec_->EchoDelayMeasured(&measured_ms)) {
        snprintf(buf, sizeof(buf),
                 "Автокалибровка измерила %d мс и зафиксировала · допуск %d мс", measured_ms,
                 codec_->RefToleranceMs());
    } else {
        snprintf(buf, sizeof(buf),
                 "Автокалибровка ищет задержку эха · сейчас %d мс · допуск %d мс",
                 codec_->EchoDelayMs(), codec_->RefToleranceMs());
    }
    emote_lock(emote_);
    gfx_label_set_text_fmt(status_, "%s", buf);
    emote_unlock(emote_);
}

void EmoteAudioSettings::ShowRowHint(int row, bool saved) {
    if (status_ == nullptr || row < 0 || row >= static_cast<int>(sliders_.size())) {
        return;
    }
    const Slider& s = sliders_[row];
    // Формат: «что это — за что отвечает · текущее значение». Нижняя строка
    // широкая (944 px ≈ 70 знаков шрифта 20 px), поэтому detail держим коротким.
    char buf[256];
    snprintf(buf, sizeof(buf), "%s — %s · %d %s%s", s.title, s.detail, s.value, s.unit,
             saved ? " · сохранено" : "");
    emote_lock(emote_);
    gfx_label_set_text_fmt(status_, "%s", buf);
    emote_unlock(emote_);
}

bool EmoteAudioSettings::ReadGt911Report(uint8_t* points, uint16_t* x, uint16_t* y) {
    *points = 0;
    if (touch_io_ == nullptr) {
        return false;
    }
    uint8_t status = 0;
    if (esp_lcd_panel_io_rx_param(touch_io_, kGt911StatusReg, &status, 1) != ESP_OK) {
        // Шина занята кодеком или чип не ответил: состояние жеста не меняем.
        return false;
    }
    if ((status & 0x80) == 0) {
        return false;  // нового отчёта нет
    }
    const int count = status & 0x0F;
    bool ok = true;
    if (count >= 1 && count <= kGt911MaxPoints) {
        uint8_t pts[kGt911MaxPoints * kGt911PointBytes];
        if (esp_lcd_panel_io_rx_param(touch_io_, kGt911PointReg, pts,
                                      count * kGt911PointBytes) == ESP_OK) {
            *x = static_cast<uint16_t>(pts[1] | (pts[2] << 8));
            *y = static_cast<uint16_t>(pts[3] | (pts[4] << 8));
            *points = static_cast<uint8_t>(count);
        } else {
            ok = false;
        }
    }
    // Статус обязан быть обнулён после чтения, иначе GT911 больше ничего не
    // пришлёт (этот шаг и был причиной «тач молчит» в эталоне).
    const uint8_t clear = 0;
    esp_lcd_panel_io_tx_param(touch_io_, kGt911StatusReg, &clear, 1);
    return ok;
}

void EmoteAudioSettings::ProbeGt911Config() {
    if (touch_io_ == nullptr) {
        ESP_LOGW(TAG, "Нет I2C-шины GT911 — экран настроек будет без тача");
        return;
    }
    uint8_t cfg[8] = {};
    if (esp_lcd_panel_io_rx_param(touch_io_, kGt911ConfigReg, cfg, sizeof(cfg)) != ESP_OK) {
        ESP_LOGW(TAG, "Конфиг GT911 не читается: беру координаты как есть (%dx%d)", panel_w_,
                 panel_h_);
        return;
    }
    chip_x_max_ = cfg[1] | (cfg[2] << 8);
    chip_y_max_ = cfg[3] | (cfg[4] << 8);
    // Ориентация: если чип описывает портрет, а панель ландшафтная (или
    // наоборот) — координаты переставлены местами и тапы мимо всех зон.
    const bool chip_portrait = chip_x_max_ > 0 && chip_y_max_ > 0 && chip_x_max_ < chip_y_max_;
    const bool panel_portrait = panel_h_ > panel_w_;
    swap_xy_ = chip_portrait != panel_portrait;
    ESP_LOGI(TAG,
             "GT911 конфиг: версия=0x%02X Xout=%d Yout=%d точек_макс=%u; панель %dx%d → swap=%d "
             "mirror=%d/%d",
             cfg[0], chip_x_max_, chip_y_max_, cfg[5], panel_w_, panel_h_, (int)swap_xy_,
             (int)mirror_x_, (int)mirror_y_);
    if (cfg[0] == 0x00 || cfg[0] == 0xFF) {
        ESP_LOGW(TAG,
                 "Конфиг GT911 выглядит пустым (версия 0x%02X): чип может не отдавать касания",
                 cfg[0]);
    }
}

void EmoteAudioSettings::MapPoint(uint16_t raw_x, uint16_t raw_y, int* x, int* y) const {
    int px = raw_x;
    int py = raw_y;
    if (swap_xy_) {
        const int t = px;
        px = py;
        py = t;
    }
    // Диапазон чипа не обязан равняться размеру панели: приводим к пикселям.
    const int src_x = swap_xy_ ? chip_y_max_ : chip_x_max_;
    const int src_y = swap_xy_ ? chip_x_max_ : chip_y_max_;
    if (src_x > 0 && src_x != panel_w_) {
        px = static_cast<int>(static_cast<int64_t>(px) * panel_w_ / src_x);
    }
    if (src_y > 0 && src_y != panel_h_) {
        py = static_cast<int>(static_cast<int64_t>(py) * panel_h_ / src_y);
    }
    if (mirror_x_) {
        px = panel_w_ - 1 - px;
    }
    if (mirror_y_) {
        py = panel_h_ - 1 - py;
    }
    *x = Clamp(px, 0, panel_w_ - 1);
    *y = Clamp(py, 0, panel_h_ - 1);
}

void EmoteAudioSettings::DiagnoseTouch(int x, int y, int64_t now) {
    if (touch_dbg_left_ <= 0 || now - last_diag_ms_ < 250) {
        return;
    }
    --touch_dbg_left_;
    last_diag_ms_ = now;
    const int hit = HitRegion(x, y);
    if (hit < 0) {
        ESP_LOGI(TAG, "ТАЧ: экран (%d,%d) — мимо всех зон (открыт=%d)", x, y, open_ ? 1 : 0);
        return;
    }
    const Region& r = regions_[hit];
    const char* kind = "зона";
    if (r.kind == Kind::kEntry) kind = "вход";
    if (r.kind == Kind::kMinus) kind = "минус";
    if (r.kind == Kind::kPlus) kind = "плюс";
    if (r.kind == Kind::kTrack) kind = "дорожка";
    if (r.kind == Kind::kAction) kind = "кнопка";
    ESP_LOGI(TAG, "ТАЧ: экран (%d,%d) → зона %d «%s» (строка %d, действие %d)", x, y, hit, kind,
             r.row, r.action);
}

void EmoteAudioSettings::PollLoop() {
    if (touch_io_ == nullptr || stop_task_) {
        return;
    }
    if (!EnsureBuilt()) {
        return;
    }
    const int64_t now = NowMs();
    uint8_t points = 0;
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    const bool fresh = ReadGt911Report(&points, &raw_x, &raw_y);
    if (fresh && points > 0) {
        MapPoint(raw_x, raw_y, &last_x_, &last_y_);
        last_report_ms_ = now;
        down_ = true;
    } else if (fresh) {
        // Отчёт с нулём точек — это и есть «палец убрали».
        down_ = false;
    } else if (down_ && now - last_report_ms_ >= kHoldGraceMs) {
        // Чип молчит дольше времени между отчётами: считаем, что касания нет.
        // Без этого шага удержание пальца никогда не превращалось бы в
        // отпускание, а кнопки на отпускании не срабатывали бы вовсе.
        down_ = false;
    }

    const bool down = down_;
    const int x = last_x_;
    const int y = last_y_;

    if (!down) {
        if (pressed_ >= 0) {
            const Region& r = regions_[pressed_];
            // GT911 на отпускании координаты не отдаёт, поэтому сверяемся с
            // последней запомненной точкой: кнопка срабатывает, только если
            // палец ушёл в её пределах.
            const bool inside = RegionContains(r, last_x_, last_y_);
            if (r.kind == Kind::kAction) {
                if (r.action >= 0 && r.action < 4 && buttons_[r.action] != nullptr) {
                    emote_lock(emote_);
                    gfx_label_set_bg_color(buttons_[r.action], GFX_COLOR_HEX(kColorButton));
                    emote_unlock(emote_);
                }
                if (inside) {
                    RunAction(r.action);
                }
            } else if (r.kind == Kind::kTrack || r.kind == Kind::kMinus || r.kind == Kind::kPlus) {
                // Палец убрали: дотягиваем значение до финального (перетаскивание
                // по дорожке мог ограничивать троттлинг) и сохраняем в NVS один раз.
                ApplyRow(r.row, false);
                codec_->SaveEchoTuning();
                // Показываем, что именно и на сколько изменилось: после «просто
                // цифра» пользователь понимает, за что отвечает строка.
                ShowRowHint(r.row, true);
            } else if (r.kind == Kind::kEntry && inside) {
                Open();
            }
        }
        pressed_ = -1;
        return;
    }

    const int hit = HitRegion(x, y);
    if (pressed_ < 0) {
        DiagnoseTouch(x, y, now);
        pressed_ = hit;
        pressed_at_ms_ = now;
        last_repeat_ms_ = now;
        if (hit < 0) {
            return;
        }
        const Region& nr = regions_[hit];
        if (nr.kind == Kind::kAction) {
            const int action = nr.action;
            if (action >= 0 && action < 4 && buttons_[action] != nullptr) {
                // Тактильный отклик: подсвечиваем кнопку, пока палец на месте.
                emote_lock(emote_);
                gfx_label_set_bg_color(buttons_[action], GFX_COLOR_HEX(kColorButtonActive));
                emote_unlock(emote_);
            }
        } else if (nr.kind == Kind::kMinus || nr.kind == Kind::kPlus) {
            // Шаг применяется сразу на касании — так «-»/«+» ощущаются как
            // настоящие кнопки, а удержание ниже даёт автоповтор.
            StepValue(nr.row, nr.kind == Kind::kMinus ? -1 : 1);
            ShowRowHint(nr.row);
        } else if (nr.kind == Kind::kTrack) {
            const int usable = nr.w - 8 > 1 ? nr.w - 8 : 1;
            const int rel = Clamp((int)x - nr.x, 0, usable);
            Slider& s = sliders_[nr.row];
            SetValue(nr.row, s.min_value + rel * (s.max_value - s.min_value) / usable, true, true);
            ShowRowHint(nr.row);
        }
        return;
    }

    const Region& r = regions_[pressed_];
    if (r.kind == Kind::kTrack) {
        const int usable = r.w - 8 > 1 ? r.w - 8 : 1;
        const int rel = Clamp((int)x - r.x, 0, usable);
        Slider& s = sliders_[r.row];
        const int before = s.value;
        // Палец ведёт значение по дорожке: это и есть «слайдер» в смысле
        // пользователя, а не только полоска-индикатор.
        SetValue(r.row, s.min_value + rel * (s.max_value - s.min_value) / usable, true, true);
        if (s.value != before) {
            // Расшифровку обновляем только когда число реально сдвинулось:
            // иначе одна и та же строка перерисовывается каждые 30 мс.
            ShowRowHint(r.row);
        }
        return;
    }
    if (r.kind == Kind::kMinus || r.kind == Kind::kPlus) {
        // Удержание кнопки шага — автоповтор после kRepeatDelayMs с периодом
        // kRepeatPeriodMs: иначе «Громкость» от 0 до 100 пришлось бы набирать
        // пятьюдесятью касаниями.
        if (now - pressed_at_ms_ >= kRepeatDelayMs && now - last_repeat_ms_ >= kRepeatPeriodMs) {
            last_repeat_ms_ = now;
            StepValue(r.row, r.kind == Kind::kMinus ? -1 : 1);
            ShowRowHint(r.row);
        }
    }
}

}  // namespace emote
