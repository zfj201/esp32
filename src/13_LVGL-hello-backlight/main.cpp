#include <Arduino.h>

#include <LcdIli9341.h>
#include <LvglPort.h>
#include <lvgl.h>

#include "Brightness.h"

// ============================================================
// 第 13 课：Hello LVGL + Slider 控制 LCD 背光
// ============================================================

namespace
{
constexpr uint8_t BACKLIGHT_PWM_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_PWM_FREQUENCY = 5000;
constexpr uint8_t BACKLIGHT_PWM_RESOLUTION = 8;
constexpr int32_t INITIAL_BRIGHTNESS = 60;

lv_obj_t* g_brightnessLabel = nullptr;

void setBacklightBrightness(int32_t percent)
{
    ledcWrite(
        BACKLIGHT_PWM_CHANNEL,
        brightnessPercentToDuty(static_cast<uint8_t>(percent))
    );
}

void onBrightnessChanged(lv_event_t* event)
{
    lv_obj_t* slider = lv_event_get_target_obj(event);
    const int32_t percent = lv_slider_get_value(slider);

    lv_label_set_text_fmt(
        g_brightnessLabel,
        "Brightness: %" LV_PRId32 "%%",
        percent
    );

    setBacklightBrightness(percent);
}

void beginBacklightPwm()
{
    ledcSetup(
        BACKLIGHT_PWM_CHANNEL,
        BACKLIGHT_PWM_FREQUENCY,
        BACKLIGHT_PWM_RESOLUTION
    );
    ledcAttachPin(LCD_BL, BACKLIGHT_PWM_CHANNEL);
    setBacklightBrightness(INITIAL_BRIGHTNESS);
}

void createUserInterface()
{
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF8FAFC), 0);

    lv_obj_t* helloLabel = lv_label_create(screen);
    lv_label_set_text(helloLabel, "Hello LVGL");
    lv_obj_set_style_text_color(helloLabel, lv_color_hex(0x2563EB), 0);
    lv_obj_align(helloLabel, LV_ALIGN_CENTER, 0, -65);

    g_brightnessLabel = lv_label_create(screen);
    lv_label_set_text_fmt(
        g_brightnessLabel,
        "Brightness: %" LV_PRId32 "%%",
        INITIAL_BRIGHTNESS
    );
    lv_obj_align(g_brightnessLabel, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t* slider = lv_slider_create(screen);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, INITIAL_BRIGHTNESS, LV_ANIM_OFF);
    lv_obj_set_size(slider, 190, 20);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 35);
    lv_obj_add_event_cb(
        slider,
        onBrightnessChanged,
        LV_EVENT_VALUE_CHANGED,
        nullptr
    );
}
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("13_LVGL-hello-backlight: start");

    const bool touchReady = lvglPortBegin();
    beginBacklightPwm();
    createUserInterface();

    if (!touchReady)
    {
        Serial.println("13_LVGL-hello-backlight: GT911 unavailable");
    }
}

void loop()
{
    lvglPortTask();
}
