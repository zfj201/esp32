#include <Arduino.h>
#include <Wire.h>

// GT911 支持的两个 7 位 I²C 地址
constexpr uint8_t GT911_ADDRESS_1 = 0x5D;
constexpr uint8_t GT911_ADDRESS_2 = 0x14;

// 产品信息起始寄存器
constexpr uint16_t GT911_REG_PRODUCT_INFO = 0x8140;

// 从 0x8140 到 0x814A，一共 11 字节
constexpr size_t GT911_PRODUCT_INFO_LENGTH = 11;

// 程序运行时检测到的真实地址
uint8_t gt911Address = 0;

uint16_t makeUint16(uint8_t highByte, uint8_t lowByte) {
    return (highByte << 8) | lowByte;
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
void setup() {
  Serial.begin(115200);
  delay(1500);
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
    Serial.println("GT911 地址检测成功: " + String(gt911Address));
    readProductInfo();
}

void loop() {
  
}