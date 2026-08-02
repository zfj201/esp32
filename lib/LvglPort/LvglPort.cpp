#include "LvglPort.h"

#include <Arduino.h>
#include <Gt911.h>
#include <LcdIli9341.h>
#include <lvgl.h>

#include "LvglTouchTracker.h"

namespace
{
constexpr uint32_t DRAW_BUFFER_LINES = 32;
constexpr uint32_t BYTES_PER_PIXEL = 2;
constexpr uint32_t DRAW_BUFFER_SIZE =
    LCD_WIDTH * DRAW_BUFFER_LINES * BYTES_PER_PIXEL;

alignas(4) uint8_t g_drawBuffer[DRAW_BUFFER_SIZE];

LvglTouchTracker g_touchTracker;
bool g_touchReady = false;

uint32_t lvglTickMillis()
{
    return millis();
}

void lvglDisplayFlush(
    lv_display_t* display,
    const lv_area_t* area,
    uint8_t* pixelMap)
{
    const uint16_t width =
        static_cast<uint16_t>(area->x2 - area->x1 + 1);
    const uint16_t height =
        static_cast<uint16_t>(area->y2 - area->y1 + 1);

    lcdDrawRGB565Bytes(
        area->x1,
        area->y1,
        width,
        height,
        pixelMap
    );

    lv_display_flush_ready(display);
}

void onGt911Event(
    Gt911TouchEventType type,
    const Gt911TouchPoint& point)
{
    int32_t lcdX = 0;
    int32_t lcdY = 0;
    gt911MapToLcd(point.x, point.y, lcdX, lcdY);

    switch (type)
    {
        case Gt911TouchEventType::Pressed:
            g_touchTracker.handlePressed(point.id, lcdX, lcdY);
            break;

        case Gt911TouchEventType::Moved:
            g_touchTracker.handleMoved(point.id, lcdX, lcdY);
            break;

        case Gt911TouchEventType::Released:
            g_touchTracker.handleReleased(point.id);
            break;
    }
}

void lvglTouchRead(lv_indev_t*, lv_indev_data_t* data)
{
    data->point.x = static_cast<int16_t>(g_touchTracker.x());
    data->point.y = static_cast<int16_t>(g_touchTracker.y());
    data->state = g_touchTracker.isPressed()
        ? LV_INDEV_STATE_PRESSED
        : LV_INDEV_STATE_RELEASED;
}
}

bool lvglPortBegin()
{
    lcdInit();

    lv_init();
    lv_tick_set_cb(lvglTickMillis);

    lv_display_t* display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    if (display == nullptr)
    {
        Serial.println("LvglPort: display creation failed");
        return false;
    }

    // ESP32 是小端存储，而 ILI9341 的 SPI 像素要求高字节先发送。
    // 让 LVGL 直接生成交换后的 RGB565 字节流，flush 时无需逐像素转换。
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(
        display,
        g_drawBuffer,
        nullptr,
        sizeof(g_drawBuffer),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );
    lv_display_set_flush_cb(display, lvglDisplayFlush);

    g_touchReady = gt911Begin();
    if (!g_touchReady)
    {
        Serial.println("LvglPort: GT911 initialization failed");
        return false;
    }

    gt911SetEventCallback(onGt911Event);

    lv_indev_t* touchInput = lv_indev_create();
    if (touchInput == nullptr)
    {
        Serial.println("LvglPort: input device creation failed");
        return false;
    }

    lv_indev_set_type(touchInput, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touchInput, lvglTouchRead);

    Serial.printf(
        "LvglPort: %ux%u, RGB565 swapped, %lu-byte buffer\n",
        static_cast<unsigned>(LCD_WIDTH),
        static_cast<unsigned>(LCD_HEIGHT),
        static_cast<unsigned long>(sizeof(g_drawBuffer))
    );

    return true;
}

void lvglPortTask()
{
    if (g_touchReady)
    {
        gt911Service();
    }

    lv_timer_handler();
    delay(5);
}
