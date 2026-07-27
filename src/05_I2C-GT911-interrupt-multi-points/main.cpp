#include <Arduino.h>
#include <Wire.h>

// 定义中断引脚
constexpr int INTERRUPT_PIN = 3;

// GT911 支持的两个 7 位 I²C 地址
constexpr uint8_t GT911_ADDRESS_1 = 0x5D;
constexpr uint8_t GT911_ADDRESS_2 = 0x14;

// 产品信息起始寄存器
constexpr uint16_t GT911_REG_PRODUCT_INFO = 0x8140;

// 产品信息从 0x8140 到 0x814A，一共 11 字节
constexpr size_t GT911_PRODUCT_INFO_LENGTH = 11;
// 触摸状态寄存器
constexpr uint16_t REG_TOUCH_STATUS = 0x814E;

// 第一个触摸点数据的起始地址
constexpr uint16_t REG_POINT_1 = 0x814F;

// Module_Switch1：含 INT 触发方式（bit1~0）
constexpr uint16_t REG_MODULE_SWITCH1 = 0x804D;
constexpr uint8_t INT_TRIGGER_MASK = 0x03;

// 一个触摸点占 8 字节
constexpr size_t POINT_DATA_LENGTH = 8;
// 最大触摸点数量
constexpr size_t MAX_TOUCH_POINTS = 5;

// ============================================================
// 0x814E 状态位掩码
// ============================================================

// 只保留bit7：坐标数据是否就绪 0x80 = 1000 0000
constexpr uint8_t STATUS_READY_MASK = 0x80;

// 只保留bit6：是否检测到手掌或大面积触摸 0x40 = 0100 0000
constexpr uint8_t STATUS_PALM_MASK = 0x40;

// 只保留bit4：是否有触摸按键事件 0x10 = 0001 0000
constexpr uint8_t STATUS_KEY_MASK = 0x10;

// 只保留bit3~0：触摸点数量 0x0F = 0000 1111
constexpr uint8_t TOUCH_COUNT_MASK = 0x0F;

// 程序运行时检测到的真实地址
uint8_t gt911Address = 0;
// 用于判断是否从“触摸中”变成“已松开”
bool wasTouching = false;
// ISR 置位，loop 中处理
volatile bool touchIrqPending = false;

struct TouchPoint {
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
};

struct TouchFrame {
    uint8_t count;
    TouchPoint points[MAX_TOUCH_POINTS];
};

// 上一帧触摸点数据
TouchFrame previousFrame = {};

enum class TouchEventType {
    Pressed,
    Moved,
    Released
};

enum class FrameReadResult {
    NoData,
    Success,
    Error
};

uint16_t makeUint16(uint8_t highByte, uint8_t lowByte) {
    return (highByte << 8) | lowByte;
}

bool writeRegister8(uint8_t deviceAddress, uint16_t registerAddress, uint8_t value) {
    Wire.beginTransmission(deviceAddress);
    Wire.write(registerAddress >> 8);
    Wire.write(registerAddress & 0xFF);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegisters16(uint8_t deviceAddress, uint16_t registerAddress, uint8_t* buffer, size_t length) {
    Wire.beginTransmission(deviceAddress);
    uint8_t highByte = (registerAddress >> 8) & 0xFF;
    uint8_t lowByte = registerAddress & 0xFF;
    Wire.write(highByte);
    Wire.write(lowByte);
    uint8_t transmissionResult = Wire.endTransmission(false);
    if (transmissionResult != 0) {
        return false;
    }
    uint8_t requestResult = Wire.requestFrom(deviceAddress, length);
    if (requestResult != length) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        buffer[i] = Wire.read();
    }
    return true;
}

uint8_t detectGt911Address() {
    uint8_t addressList[] = {GT911_ADDRESS_1, GT911_ADDRESS_2};
    for(uint8_t address : addressList) {
        uint8_t productId[4] = {};
        if(readRegisters16(address, GT911_REG_PRODUCT_INFO, productId, 4)) {
            // 产品 ID 应该是可打印的 ASCII 字符。
            // 至少检查前三个字节，避免把其他设备误认为 GT911。
            bool looksValid =
            productId[0] >= 0x20 && productId[0] <= 0x7E &&
            productId[1] >= 0x20 && productId[1] <= 0x7E &&
            productId[2] >= 0x20 && productId[2] <= 0x7E;

            if (looksValid) {
                return address;
            }
        }
    }
    return 0;

}

void readProductInfo() {
    uint8_t buffer[GT911_PRODUCT_INFO_LENGTH] = {};
    bool success = readRegisters16(gt911Address, GT911_REG_PRODUCT_INFO, buffer, GT911_PRODUCT_INFO_LENGTH);
    if(!success) {
        Serial.println("读取产品信息失败");
        return;
    }
    Serial.println("读取产品信息成功");
    // 前四个字节是 ASCII 产品 ID。
    // 多准备一个字节，用于放字符串结束符 '\0'。
    char productId[5] = {
        static_cast<char>(buffer[0]),
        static_cast<char>(buffer[1]),
        static_cast<char>(buffer[2]),
        static_cast<char>(buffer[3]),
        '\0'
    };
    // 固件版本：
    // data[4] = 低字节
    // data[5] = 高字节
    uint16_t firmwareVersion = makeUint16(buffer[5], buffer[4]);

    //x分辨率
    uint16_t xResolution = makeUint16(buffer[7], buffer[6]);
    //y分辨率
    uint16_t yResolution = makeUint16(buffer[9], buffer[8]);
    // data[10] 对应 0x814A
    uint8_t vendorId = buffer[10];
    Serial.println("--------------------------------");
    Serial.println("Product ID: " + String(productId));
    Serial.println("Firmware Version: " + String(firmwareVersion));
    Serial.println("X Resolution: " + String(xResolution));
    Serial.println("Y Resolution: " + String(yResolution));
    Serial.println("Vendor ID: " + String(vendorId));
}

bool clearTouchStatus() {
    return writeRegister8(gt911Address, REG_TOUCH_STATUS, 0x00);
}

FrameReadResult readTouchFrame(TouchFrame& frame)
{
    frame.count = 0;

    uint8_t status = 0;

    // 先读 0x814E
    if (!readRegisters16(
            gt911Address,
            REG_TOUCH_STATUS,
            &status,
            1)) {
        Serial.println("读取触摸状态失败");
        return FrameReadResult::Error;
    }

    // bit7=0：没有新的有效数据
    if ((status & STATUS_READY_MASK) == 0) {
        return FrameReadResult::NoData;
    }

    uint8_t touchCount =
        status & TOUCH_COUNT_MASK;

    // 普通 GT911 配置最多支持 5 点
    if (touchCount > MAX_TOUCH_POINTS) {
        Serial.print("异常触摸点数量：");
        Serial.println(touchCount);

        clearTouchStatus();
        return FrameReadResult::Error;
    }

    frame.count = touchCount;

    // touchCount=0 是一帧有效的“全部松开”状态
    if (touchCount == 0) {
        if (!clearTouchStatus()) {
            Serial.println("清除松开状态失败");
            return FrameReadResult::Error;
        }

        return FrameReadResult::Success;
    }

    // 每个点 8 字节
    const size_t dataLength =
        static_cast<size_t>(touchCount) *
        POINT_DATA_LENGTH;

    uint8_t rawData[
        MAX_TOUCH_POINTS * POINT_DATA_LENGTH
    ] = {};

    // 从第一个触摸点开始连续读取所有点
    if (!readRegisters16(
            gt911Address,
            REG_POINT_1,
            rawData,
            dataLength)) {
        Serial.println("读取多点坐标失败");

        // 暂不清除，让 GT911 继续提醒
        return FrameReadResult::Error;
    }

    // 解析每个触摸点
    for (size_t index = 0;
         index < touchCount;
         ++index) {

        const size_t offset =
            index * POINT_DATA_LENGTH;

        TouchPoint& point =
            frame.points[index];

        point.id = rawData[offset + 0];

        point.x = makeUint16(
            rawData[offset + 2],
            rawData[offset + 1]
        );

        point.y = makeUint16(
            rawData[offset + 4],
            rawData[offset + 3]
        );

        point.size = makeUint16(
            rawData[offset + 6],
            rawData[offset + 5]
        );

        // offset + 7 是保留字节
    }

    // 所有坐标都已经读取并保存，最后再清状态
    if (!clearTouchStatus()) {
        Serial.println("清除触摸状态失败");
        return FrameReadResult::Error;
    }

    return FrameReadResult::Success;
}

// 根据 ID 查找触摸点在当前帧或上一帧中是否存在？
const TouchPoint* findPointById(
    const TouchFrame& frame,
    uint8_t id)
{
    for (size_t index = 0;
         index < frame.count;
         ++index) {

        if (frame.points[index].id == id) {
            return &frame.points[index];
        }
    }

    return nullptr;
}

void printTouchEvent(
    TouchEventType type,
    const TouchPoint& point)
{
    switch (type) {
        case TouchEventType::Pressed:
            Serial.print("PRESSED  ");
            break;

        case TouchEventType::Moved:
            Serial.print("MOVED    ");
            break;

        case TouchEventType::Released:
            Serial.print("RELEASED ");
            break;
    }

    Serial.print("id=");
    Serial.print(point.id);

    Serial.print(" x=");
    Serial.print(point.x);

    Serial.print(" y=");
    Serial.print(point.y);

    Serial.print(" size=");
    Serial.println(point.size);
}

void processTouchEvents(
    const TouchFrame& currentFrame)
{
    // --------------------------------------------------------
    // 第一部分：
    // 遍历当前帧，生成 Pressed 或 Moved
    // --------------------------------------------------------

    for (size_t index = 0;
         index < currentFrame.count;
         ++index) {

        const TouchPoint& currentPoint =
            currentFrame.points[index];

        const TouchPoint* previousPoint =
            findPointById(
                previousFrame,
                currentPoint.id
            );

        // 上一帧没有该 ID：
        // 说明这根手指刚出现
        if (previousPoint == nullptr) {
            printTouchEvent(
                TouchEventType::Pressed,
                currentPoint
            );

            continue;
        }

        // 同一个 ID 仍然存在，并且坐标改变：
        // 说明手指移动
        bool coordinateChanged =
            previousPoint->x != currentPoint.x ||
            previousPoint->y != currentPoint.y;

        if (coordinateChanged) {
            printTouchEvent(
                TouchEventType::Moved,
                currentPoint
            );
        }
    }

    // --------------------------------------------------------
    // 第二部分：
    // 遍历上一帧，查找消失的 ID，生成 Released
    // --------------------------------------------------------

    for (size_t index = 0;
         index < previousFrame.count;
         ++index) {

        const TouchPoint& previousPoint =
            previousFrame.points[index];

        const TouchPoint* currentPoint =
            findPointById(
                currentFrame,
                previousPoint.id
            );

        // 上一帧存在，当前帧不存在：
        // 说明该手指已经松开
        if (currentPoint == nullptr) {
            printTouchEvent(
                TouchEventType::Released,
                previousPoint
            );
        }
    }

    // 当前帧保存为下一轮的上一帧
    previousFrame = currentFrame;
}

void processPendingTouchInterrupt()
{
    TouchFrame currentFrame = {};

    FrameReadResult result =
        readTouchFrame(currentFrame);

    switch (result) {
        case FrameReadResult::NoData:
            // 收到了边沿，但 bit7 没有有效数据
            return;

        case FrameReadResult::Error:
            return;

        case FrameReadResult::Success:
            processTouchEvents(currentFrame);
            return;
    }
}

uint8_t readIntTriggerMode() {
    uint8_t value = 0;
    if (!readRegisters16(gt911Address, REG_MODULE_SWITCH1, &value, 1)) {
        return 0xFF;
    }
    return value & INT_TRIGGER_MASK;
}

int mapIntTriggerToArduinoMode(uint8_t triggerMode) {
    switch (triggerMode) {
        case 0:
            return RISING;
        case 1:
            return FALLING;
        // 低电平有效：抓进入低电平的瞬间，避免 ONLOW 反复触发
        case 2:
            return FALLING;
        // 高电平有效：抓进入高电平的瞬间，避免 ONHIGH 反复触发
        case 3:
            return RISING;
        default:
            return -1;
    }
}

void IRAM_ATTR onGt911Interrupt() {
    touchIrqPending = true;
}

bool setupTouchInterrupt() {
    pinMode(INTERRUPT_PIN, INPUT);

    uint8_t triggerMode = readIntTriggerMode();
    if (triggerMode == 0xFF) {
        Serial.println("读取 INT 触发方式失败 (0x804D)");
        return false;
    }

    const char* triggerName = "Unknown";
    switch (triggerMode) {
        case 0:
            triggerName = "Rising edge";
            break;
        case 1:
            triggerName = "Falling edge";
            break;
        case 2:
            triggerName = "Low level";
            break;
        case 3:
            triggerName = "High level";
            break;
    }
    Serial.printf("INT trigger mode (0x804D[1:0]): %u (%s)\n", triggerMode, triggerName);

    int arduinoMode = mapIntTriggerToArduinoMode(triggerMode);
    if (arduinoMode < 0) {
        Serial.println("不支持的 INT 触发方式");
        return false;
    }

    // 挂中断前清掉残留坐标状态，避免首次触摸丢事件
    if (!clearTouchStatus()) {
        Serial.println("清除旧触摸状态失败");
        return false;
    }

    touchIrqPending = false;
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), onGt911Interrupt, arduinoMode);
    Serial.printf("Touch interrupt attached on GPIO %d\n", INTERRUPT_PIN);
    return true;
}

void setup() {
  Serial.begin(115200);
  delay(2500);
  if (!Wire.begin(I2C_SDA, I2C_SCL, 100000)) {
        Serial.println("I2C 初始化失败");
        while (true) {
            delay(1000);
        }
    }

    Wire.setTimeOut(50);

    Serial.println("I2C 初始化成功");
    gt911Address = detectGt911Address();
    if(gt911Address == 0) {
        Serial.println("GT911 地址检测失败");
        while (true) {
            delay(1000);
        }
    }
    Serial.printf("GT911 地址检测成功: 0x%02X\n", gt911Address);
    readProductInfo();
    if (!setupTouchInterrupt()) {
        Serial.println("触摸中断初始化失败");
        while (true) {
            delay(1000);
        }
    }
}

void loop() {
    if (touchIrqPending) {
        touchIrqPending = false;
        processPendingTouchInterrupt();
    }
}