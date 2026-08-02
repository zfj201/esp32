#pragma once

#include <Arduino.h>

// ============================================================
// GT911 电容触摸公共驱动（中断读点）
//
// 通信：I2C
// 典型流程：
//   手指按下 -> GT911 更新寄存器 -> INT 触发
//   -> MCU 读 0x814E / 坐标寄存器 -> 清状态
//
// 本板约定：
//   INT = GPIO3
//   SDA / SCL = 板级 I2C_SDA / I2C_SCL
//   分辨率与 LCD 一致：240 x 320，轴向已对齐，可 1:1 映射
// ============================================================

constexpr int GT911_INT_PIN = 3;

// GT911 常见的两个 7-bit 地址
constexpr uint8_t GT911_ADDRESS_1 = 0x5D;
constexpr uint8_t GT911_ADDRESS_2 = 0x14;

constexpr uint16_t GT911_REG_PRODUCT_INFO = 0x8140;
constexpr size_t GT911_PRODUCT_INFO_LENGTH = 11;

constexpr uint16_t GT911_REG_TOUCH_STATUS = 0x814E;
constexpr uint16_t GT911_REG_POINT_1 = 0x814F;
constexpr uint16_t GT911_REG_MODULE_SWITCH1 = 0x804D;

constexpr uint8_t GT911_INT_TRIGGER_MASK = 0x03;
constexpr uint8_t GT911_STATUS_READY_MASK = 0x80;
constexpr uint8_t GT911_TOUCH_COUNT_MASK = 0x0F;

constexpr size_t GT911_POINT_DATA_LENGTH = 8;
constexpr size_t GT911_MAX_TOUCH_POINTS = 5;

// 与 LCD 一致，便于 1:1 使用。
constexpr uint16_t GT911_TOUCH_WIDTH  = 240;
constexpr uint16_t GT911_TOUCH_HEIGHT = 320;

struct Gt911TouchPoint
{
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
};

struct Gt911TouchFrame
{
    uint8_t count;
    Gt911TouchPoint points[GT911_MAX_TOUCH_POINTS];
};

enum class Gt911FrameReadResult
{
    NoData,
    Success,
    Error
};

enum class Gt911TouchEventType
{
    Pressed,
    Moved,
    Released
};

// 初始化 I2C、探测地址、打印产品信息、挂接 INT。
// 成功返回 true。
bool gt911Begin(uint32_t i2cClockHz = 100000);

// loop 中调用：若有挂起中断则读取并分发事件。
void gt911Service();

// 用户可设置的事件回调。point 始终是“该事件对应的触点”。
// Pressed / Moved：当前帧坐标
// Released：上一帧最后已知坐标
using Gt911TouchEventCallback =
    void (*)(Gt911TouchEventType type, const Gt911TouchPoint& point);

void gt911SetEventCallback(Gt911TouchEventCallback callback);

// 主动读一帧（一般由 gt911Service 内部调用）。
Gt911FrameReadResult gt911ReadTouchFrame(Gt911TouchFrame& frame);

// 将触摸坐标映射到 LCD 坐标。
// 当前板子分辨率一致，函数内部做边界裁剪；若未来分辨率变化，
// 可在此处统一改成比例换算。
void gt911MapToLcd(
    uint16_t touchX,
    uint16_t touchY,
    int32_t& lcdX,
    int32_t& lcdY
);

uint8_t gt911GetAddress();
