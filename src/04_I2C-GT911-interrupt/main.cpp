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

bool readResisters16(uint8_t deviceAddress, uint16_t registerAddress, uint8_t* buffer, size_t length) {
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
        if(readResisters16(address, GT911_REG_PRODUCT_INFO, productId, 4)) {
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
    bool success = readResisters16(gt911Address, GT911_REG_PRODUCT_INFO, buffer, GT911_PRODUCT_INFO_LENGTH);
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

bool readTouchPoints(TouchPoint& touchPoints) {
    uint8_t pointData[POINT_DATA_LENGTH] = {};
    bool success = readResisters16(gt911Address, REG_POINT_1, pointData, POINT_DATA_LENGTH);
    if(!success) {
        Serial.println("读取触摸点数据失败");
        return false;
    }
    Serial.println("读取触摸点数据成功");
    touchPoints.id = pointData[0];
    touchPoints.x = makeUint16(pointData[2], pointData[1]);
    touchPoints.y = makeUint16(pointData[4], pointData[3]);
    touchPoints.size = makeUint16(pointData[6], pointData[5]);
    return true;
}

bool clearTouchStatus() {
    return writeRegister8(gt911Address, REG_TOUCH_STATUS, 0x00);
}

uint8_t readIntTriggerMode() {
    uint8_t value = 0;
    if (!readResisters16(gt911Address, REG_MODULE_SWITCH1, &value, 1)) {
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

bool readTouchStatus() {
    uint8_t touchStatus = 0;
    bool success = readResisters16(gt911Address, REG_TOUCH_STATUS, &touchStatus, 1);
    if(!success) {
        Serial.println("读取触摸状态失败");
        return false;
    }
    // Serial.println("读取触摸状态成功");
    bool dataReady = (touchStatus & STATUS_READY_MASK) != 0;
    if(!dataReady) {
        return true;
    }
    bool isPalm = (touchStatus & STATUS_PALM_MASK) != 0;
    bool isKey = (touchStatus & STATUS_KEY_MASK) != 0;

    uint8_t touchCount = touchStatus & TOUCH_COUNT_MASK;
    if(touchCount == 0) {
        if(wasTouching) {
            Serial.println("Touch released");
        }
        wasTouching = false;
        if(!clearTouchStatus()) {
            Serial.println("Failed to clear touch status");
            return false;
        }
        return true;
    }
    // GT911 正常屏幕触摸通常配置为最多 5 点
    if (touchCount > 5) {
        Serial.print("异常触摸点数量：");
        Serial.println(touchCount);

        clearTouchStatus();
        return false;
    }
    // 走到这说明有触摸点，读取第一个
    TouchPoint touchPoint;
    if(!readTouchPoints(touchPoint)) {
        Serial.println("Failed to read touch point");
        return false;
    }
    wasTouching = true;
    // Serial.println("Touching");
    Serial.println("大面积触摸: " + String(isPalm));
    Serial.println("触摸按键: " + String(isKey));
    Serial.println("触摸点数量: " + String(touchCount));
    Serial.println("Touch ID: " + String(touchPoint.id));
    Serial.println("Touch X: " + String(touchPoint.x));
    Serial.println("Touch Y: " + String(touchPoint.y));
    Serial.println("Touch Size: " + String(touchPoint.size));
    Serial.println("--------------------------------");

    // --------------------------------------------------------
    // 最后一步：清除 Buffer Status
    // 必须在坐标读取完成后再做
    // --------------------------------------------------------

    if (!clearTouchStatus()) {
        Serial.println("清除触摸状态失败");
        return false;
    }
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
        readTouchStatus();
    }
}