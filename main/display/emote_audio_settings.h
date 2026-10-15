#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <vector>

#include <esp_lcd_panel_io.h>

#include "expression_emote.h"

class Es8311AudioCodec;

namespace emote {

class EmoteDisplay;

/**
 * Экран ручной подстройки звука эха поверх EmoteDisplay (esp_emote_expression).
 *
 * Почему это написано на «голых» label-объектах, а не на виджетах gfx:
 * компонента expression в штатном режиме не регистрирует тач (gfx_touch_add
 * нигде не вызывается) и не даёт внешних кнопок/слайдеров — есть только
 * публичный emote_create_obj_by_type("label", ...) + gfx_label_* / gfx_obj_*.
 * Поэтому слайдер собран из двух label-прямоугольников (дорожка и заполнение),
 * а касания GT911 читаются и разбираются здесь же: своя задача опроса и своя
 * обработка попаданий по прямоугольникам. Это единственный путь, который не
 * ломается при обновлении управляемой компоненты.
 *
 * Тач читается напрямую из регистров GT911 (статус 0x814E, точки с 0x814F), а
 * не через esp_lcd_touch_get_coordinates: драйвер кэширует координаты и без
 * нового отчёта чипа оставляет прежние, поэтому «палец убрали» по нему
 * определить невозможно — а именно на отпускании срабатывают кнопки. Схема
 * чтения взята из проверенного на этой же плате эталона (CamBrowser,
 * main/camos/touch.c).
 */
class EmoteAudioSettings {
public:
    // Дисплей нужен не только ради emote_handle_: экран стартует только после
    // того, как приложение смонтирует ассеты (оттуда берётся кириллический
    // шрифт). До этого момента он молча ждёт, не засоряя лог ошибками emote.
    // touch_io — I2C-шина GT911, созданная бордом (тот же panel IO, что и у
    // драйвера tача), она же даёт нам сырые отчёты о касаниях.
    EmoteAudioSettings(EmoteDisplay* display, esp_lcd_panel_io_handle_t touch_io,
                       Es8311AudioCodec* codec);
    ~EmoteAudioSettings();

    EmoteAudioSettings(const EmoteAudioSettings&) = delete;
    EmoteAudioSettings& operator=(const EmoteAudioSettings&) = delete;

    // Вход в экран виден всегда: маленькая плашка слева внизу. Клик по ней
    // открывает экран; «Готово» или долгое нажатие BOOT закрывают.
    bool CreateEntryChip();

    void Open();
    void Close();
    bool is_open() const {
        return open_;
    }

private:
    enum class Kind {
        kNone,
        kEntry,     // плашка входа
        kMinus,     // шаг вниз
        kPlus,      // шаг вверх
        kTrack,     // дорожка слайдера (ведёт значение за пальцем)
        kAction,    // кнопка действия
    };

    enum Action {
        kActionRestart = 0,  // перезапустить автопоиск задержки
        kActionReset,        // вернуть заводские значения
        kActionDiag,         // дамп регистров кодека в лог
        kActionClose,        // закрыть экран
    };

    struct Region {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        Kind kind = Kind::kNone;
        int row = -1;    // индекс параметра для kMinus/kPlus/kTrack
        int action = -1; // Action для kAction
    };

    struct Slider {
        const char* title = "";
        // Короткое «за что это» рядом с названием: пользователь должен видеть,
        // что меняет строка, не трогая её.
        const char* hint = "";
        // Подробная формулировка для нижней строки, когда палец уже на строке.
        const char* detail = "";
        const char* unit = "";
        int min_value = 0;
        int max_value = 100;
        int step = 1;
        int value = 0;
        gfx_obj_t* name = nullptr;
        gfx_obj_t* row_hint = nullptr;
        gfx_obj_t* value_label = nullptr;
        gfx_obj_t* minus = nullptr;
        gfx_obj_t* plus = nullptr;
        gfx_obj_t* track = nullptr;
        gfx_obj_t* fill = nullptr;
        // Как читать/применять значение. Держим код parameters-специфичным в
        // одном месте, а не размазываем по обработчику тача.
        int (*getter)(Es8311AudioCodec*) = nullptr;
        void (*setter)(Es8311AudioCodec*, int) = nullptr;
    };

    void PollLoop();
    bool EnsureBuilt();
    bool BuildUI();
    void DisposeUI();
    gfx_obj_t* MakeLabel(const char* obj_name, const char* text, int x, int y, int w, int h,
                         uint32_t color, uint32_t bg, bool bg_enable, gfx_text_align_t align);
    void AddRegion(int x, int y, int w, int h, Kind kind, int row = -1, int action = -1);
    int HitRegion(int x, int y) const;
    // Принадлежность точки области с учётом запаса на палец.
    bool RegionContains(const Region& r, int x, int y) const;
    // Отладка попаданий: первые несколько касаний пишем в лог вместе с
    // областью, которую они задели. Так видно, что координаты GT911 и
    // геометрия экрана действительно совпадают.
    void DiagnoseTouch(int x, int y, int64_t now);

    // ===== GT911: чтение отчётов напрямую из регистров =====
    // Возвращает true, если чип принёс НОВЫЙ отчёт (установлен бит 7 статуса),
    // и заполняет points/x/y сырыми значениями. Отсутствие нового отчёта —
    // это не «палец убран»: при удержании GT911 шлёт отчёты каждые ~10 мс, а
    // при отпускании шлёт один отчёт с points=0.
    bool ReadGt911Report(uint8_t* points, uint16_t* x, uint16_t* y);
    // Ориентация и масштаб по регистрам конфигурации чипа (0x8047..): сравнение
    // Xout/Yout с размером панели решает, нужен ли программный swap.
    void ProbeGt911Config();
    void MapPoint(uint16_t raw_x, uint16_t raw_y, int* x, int* y) const;

    // apply=false — только перерисовать (например, при открытии экрана, где
    // значения и так актуальны). apply=true с throttle — режим перетаскивания.
    void SetValue(int row, int value, bool apply, bool throttle);
    void ApplyRow(int row, bool throttle);
    void StepValue(int row, int dir);
    void RunAction(int action);
    void UpdateStatusLine();
    // Нижняя строка: чем именно является параметр, на котором сейчас палец, и
    // какое у него значение. Без этого строки выглядят как «просто цифра».
    void ShowRowHint(int row, bool saved = false);
    void UpdateSliderVisual(int row);
    bool LoadCyrillicFont();

    static void TaskEntry(void* arg);

    emote_handle_t emote_ = nullptr;
    EmoteDisplay* display_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_ = nullptr;
    Es8311AudioCodec* codec_ = nullptr;

    // Сырая геометрия тач-контроллера: Xout/Yout из его конфига и вытекающие
    // swap/mirror. Панель у EmoteDisplay рисуется 1:1 в координатах 1024x600,
    // поэтому зеркалить точки «как для LVGL» нельзя — тапы уезжают в
    // противоположный угол.
    // EmoteDisplay не заполняет width_/height_ базового Display (те заполняются
    // только в LVGL-ветке), поэтому размер панели держим сами: 1024x600.
    int panel_w_ = 1024;
    int panel_h_ = 600;
    int chip_x_max_ = 0;
    int chip_y_max_ = 0;
    bool swap_xy_ = false;
    bool mirror_x_ = false;
    bool mirror_y_ = false;

    // Состояние жеста, собранное по новым отчётам чипа.
    bool down_ = false;
    int64_t last_report_ms_ = 0;

    // Шрифт с кириллицой берём из того же ассет-раздела, что и глаза:
    // index.json -> text_font. Встроенный font_puhui_basic_20_4 русских букв не
    // имеет, поэтому label без этой подстановки показали бы квадраты.
    lv_font_t* font_ = nullptr;
    uint8_t* font_copy_ = nullptr;

    bool open_ = false;
    // UI строится один раз, когда ассеты уже смонтированы (шрифт живёт в
    // ассет-разделе). deadline_ms_ — когда сдаться и собирать без кириллицы.
    bool built_ = false;
    bool ui_ok_ = false;
    int64_t deadline_ms_ = 0;
    int64_t last_font_attempt_ms_ = 0;
    gfx_obj_t* entry_ = nullptr;

    gfx_obj_t* title_ = nullptr;
    gfx_obj_t* hint_ = nullptr;
    gfx_obj_t* status_ = nullptr;
    gfx_obj_t* buttons_[4] = {};

    std::vector<Slider> sliders_;
    std::vector<Region> regions_;

    // Состояние жеста: индекс области под пальцем, последняя известная точка
    // (на отпускании GT911 координаты не присылает) и тайминги автоповтора.
    int pressed_ = -1;
    int last_x_ = 0;
    int last_y_ = 0;
    int64_t pressed_at_ms_ = 0;
    int64_t last_repeat_ms_ = 0;
    int64_t last_apply_ms_[4] = {0, 0, 0, 0};

    // Отладка тача: сколько касаний ещё можно залогировать и когда логировали
    // в последний раз (чтобы удержание пальца не превратило лог в кашу).
    int touch_dbg_left_ = 16;
    int64_t last_diag_ms_ = 0;

    TaskHandle_t task_ = nullptr;
    volatile bool stop_task_ = false;
};

}  // namespace emote
