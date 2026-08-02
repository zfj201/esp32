#include <Arduino.h>

#include <LcdIli9341.h>
#include <LvglPort.h>
#include <lvgl.h>

// ============================================================
// 第 12 课：LVGL 多控件设置页
//
// 学习目标：
// 1. 把现有 ILI9341 / GT911 驱动接入 LVGL 9
// 2. 理解 screen -> tabview -> tab -> widget 的对象树
// 3. 使用 Dropdown / Slider / Checkbox / Button
// 4. 使用 LV_EVENT_VALUE_CHANGED / LV_EVENT_CLICKED 更新界面
// ============================================================

namespace
{
lv_obj_t* g_modeDropdown = nullptr;
lv_obj_t* g_levelSlider = nullptr;
lv_obj_t* g_levelValueLabel = nullptr;
lv_obj_t* g_rememberCheckbox = nullptr;
lv_obj_t* g_statusLabel = nullptr;

void onLevelChanged(lv_event_t* event)
{
    lv_obj_t* slider = lv_event_get_target_obj(event);
    const int32_t value = lv_slider_get_value(slider);

    lv_label_set_text_fmt(
        g_levelValueLabel,
        "%" LV_PRId32 "%%",
        value
    );
}

void onApplyClicked(lv_event_t*)
{
    char selectedMode[16] = {};
    lv_dropdown_get_selected_str(
        g_modeDropdown,
        selectedMode,
        sizeof(selectedMode)
    );

    const int32_t level = lv_slider_get_value(g_levelSlider);
    const bool remember =
        lv_obj_has_state(g_rememberCheckbox, LV_STATE_CHECKED);

    lv_label_set_text_fmt(
        g_statusLabel,
        "Applied: %s / %" LV_PRId32 "%% / %s",
        selectedMode,
        level,
        remember ? "saved" : "temporary"
    );

    Serial.printf(
        "LVGL apply: mode=%s level=%ld remember=%s\n",
        selectedMode,
        static_cast<long>(level),
        remember ? "yes" : "no"
    );
}

void createControlsTab(lv_obj_t* parent)
{
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "DEVICE SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x2563EB), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* modeLabel = lv_label_create(parent);
    lv_label_set_text(modeLabel, "Mode");
    lv_obj_align(modeLabel, LV_ALIGN_TOP_LEFT, 4, 34);

    g_modeDropdown = lv_dropdown_create(parent);
    lv_dropdown_set_options(g_modeDropdown, "Normal\nEco\nTurbo");
    lv_obj_set_size(g_modeDropdown, 138, 38);
    lv_obj_align(g_modeDropdown, LV_ALIGN_TOP_RIGHT, -4, 24);

    lv_obj_t* levelLabel = lv_label_create(parent);
    lv_label_set_text(levelLabel, "Level");
    lv_obj_align(levelLabel, LV_ALIGN_TOP_LEFT, 4, 78);

    g_levelValueLabel = lv_label_create(parent);
    lv_label_set_text(g_levelValueLabel, "50%");
    lv_obj_align(g_levelValueLabel, LV_ALIGN_TOP_RIGHT, -4, 78);

    g_levelSlider = lv_slider_create(parent);
    lv_slider_set_range(g_levelSlider, 0, 100);
    lv_slider_set_value(g_levelSlider, 50, LV_ANIM_OFF);
    lv_obj_set_size(g_levelSlider, 204, 16);
    lv_obj_align(g_levelSlider, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_add_event_cb(
        g_levelSlider,
        onLevelChanged,
        LV_EVENT_VALUE_CHANGED,
        nullptr
    );

    g_rememberCheckbox = lv_checkbox_create(parent);
    lv_checkbox_set_text(g_rememberCheckbox, "Remember setting");
    lv_obj_align(g_rememberCheckbox, LV_ALIGN_TOP_LEFT, 4, 142);

    lv_obj_t* applyButton = lv_button_create(parent);
    lv_obj_set_size(applyButton, 120, 42);
    lv_obj_align(applyButton, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_bg_color(applyButton, lv_color_hex(0x7C3AED), 0);
    lv_obj_add_event_cb(
        applyButton,
        onApplyClicked,
        LV_EVENT_CLICKED,
        nullptr
    );

    lv_obj_t* applyLabel = lv_label_create(applyButton);
    lv_label_set_text(applyLabel, "APPLY");
    lv_obj_center(applyLabel);

    g_statusLabel = lv_label_create(parent);
    lv_label_set_text(g_statusLabel, "Adjust controls, then tap APPLY");
    lv_obj_set_width(g_statusLabel, 210);
    lv_obj_set_style_text_align(g_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_statusLabel, lv_color_hex(0x64748B), 0);
    lv_obj_align(g_statusLabel, LV_ALIGN_TOP_MID, 0, 232);
}

void createAboutTab(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(
        label,
        "LVGL 9.5 CONTROL LAB\n\n"
        "Display: ILI9341 240x320\n"
        "Touch: GT911 over I2C\n"
        "Render: RGB565 partial buffer\n\n"
        "Lesson 12"
    );
    lv_obj_set_width(label, 210);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void createUserInterface(bool touchReady)
{
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF1F5F9), 0);

    lv_obj_t* tabView = lv_tabview_create(screen);
    lv_obj_set_size(tabView, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_center(tabView);
    lv_tabview_set_tab_bar_size(tabView, 36);

    lv_obj_t* controlsTab = lv_tabview_add_tab(tabView, "Controls");
    lv_obj_t* aboutTab = lv_tabview_add_tab(tabView, "About");

    createControlsTab(controlsTab);
    createAboutTab(aboutTab);

    if (!touchReady)
    {
        lv_label_set_text(
            g_statusLabel,
            "GT911 init failed - check Serial"
        );
        lv_obj_set_style_text_color(g_statusLabel, lv_color_hex(0xDC2626), 0);
    }
}
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("12_LVGL-controls: start");

    const bool touchReady = lvglPortBegin();
    createUserInterface(touchReady);

    Serial.println("12_LVGL-controls: UI created");
}

void loop()
{
    lvglPortTask();
}
