#include <Arduino.h>
#include <Wire.h>

// I2C_SDA / I2C_SCL 已由 esp32s3box 的 pins_arduino.h 定义为 8 / 18

constexpr uint8_t REG_WHO_AM_I    = 0x75;
// 电源管理寄存器地址
constexpr uint8_t REG_PWR_MGMT0   = 0x1F;
// 加速度配置寄存器地址
constexpr uint8_t REG_ACCEL_CFG0  = 0x21;
// 加速度数据起始地址
constexpr uint8_t REG_ACCEL_DATA  = 0x0B;
// 陀螺仪配置寄存器地址
constexpr uint8_t REG_GYRO_CONFIG0 = 0x20;
// 陀螺仪配置 250DPS 100HZ
constexpr uint8_t GYRO_CONFIG_250DPS_100HZ = 0x69;
// 加速度配置 2G 100HZ
constexpr uint8_t ACCEL_CONFIG_2G_100HZ = 0x69;
// ICM42607 传感器 ID
constexpr uint8_t ICM42607_ID = 0x60;
// ICM42607 地址
uint8_t imuAddress = 0;

// 写入寄存器
bool writeRegister8(
    uint8_t deviceAddress,
    uint8_t registerAddress,
    uint8_t value)
{
    Wire.beginTransmission(deviceAddress);
    Wire.write(registerAddress);
    Wire.write(value);

    return Wire.endTransmission() == 0;
}

bool readRegister8(
    uint8_t deviceAddress,
    uint8_t registerAddress,
    uint8_t &value)
{
    Wire.beginTransmission(deviceAddress);
    Wire.write(registerAddress);

    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    size_t received = Wire.requestFrom(
        static_cast<uint16_t>(deviceAddress),
        static_cast<size_t>(1),
        true
    );

    if (received != 1 || !Wire.available()) {
        return false;
    }

    value = Wire.read();
    return true;
}

bool readRegisters(
    uint8_t deviceAddress,
    uint8_t startRegister,
    uint8_t *buffer,
    size_t length)
{
    Wire.beginTransmission(deviceAddress);
    Wire.write(startRegister);

    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    size_t received = Wire.requestFrom(
        static_cast<uint16_t>(deviceAddress),
        length,
        true
    );

    if (received != length) {
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

uint8_t findICM42607()
{
    const uint8_t addresses[] = {0x68, 0x69};

    for (uint8_t address : addresses) {
        uint8_t whoAmI = 0;

        if (readRegister8(address, REG_WHO_AM_I, whoAmI)) {
            Serial.printf(
                "检测地址 0x%02X，WHO_AM_I=0x%02X\n",
                address,
                whoAmI
            );

            if (whoAmI == ICM42607_ID) {
                return address;
            }
        }
    }

    return 0;
}

bool initSixAxis()
{
    // 加速度：±2g、100Hz
    if (!writeRegister8(
            imuAddress,
            REG_ACCEL_CFG0,
            ACCEL_CONFIG_2G_100HZ)) {
        return false;
    }

    // 陀螺仪：±250dps、100Hz
    if (!writeRegister8(
            imuAddress,
            REG_GYRO_CONFIG0,
            GYRO_CONFIG_250DPS_100HZ)) {
        return false;
    }

    // 0x0F：
    // GYRO_MODE  = 11 → 陀螺仪低噪声模式
    // ACCEL_MODE = 11 → 加速度计低噪声模式
    if (!writeRegister8(
            imuAddress,
            REG_PWR_MGMT0,
            0x0F)) {
        return false;
    }

    // 陀螺仪启动时间比加速度计长，约 30ms
    // 使用 50ms 留出余量
    delay(50);

    return true;
}

int16_t makeInt16(uint8_t highByte, uint8_t lowByte)
{
    uint16_t value =
        (static_cast<uint16_t>(highByte) << 8) |
        static_cast<uint16_t>(lowByte);

    return static_cast<int16_t>(value);
}

void setup()
{
    Serial.begin(115200);
    delay(1500);

    if (!Wire.begin(I2C_SDA, I2C_SCL, 100000)) {
        Serial.println("I2C 初始化失败");
        while (true) {
            delay(1000);
        }
    }

    Wire.setTimeOut(50);

    imuAddress = findICM42607();

    if (imuAddress == 0) {
        Serial.println("没有找到 ICM42607");
        while (true) {
            delay(1000);
        }
    }

    Serial.printf(
        "ICM42607 地址：0x%02X\n",
        imuAddress
    );

    if (!initSixAxis()) {
        Serial.println("六轴传感器初始化失败");
    
        while (true) {
            delay(1000);
        }
    }

    Serial.println("六轴传感器初始化成功");
}

void loop()
{
    uint8_t data[12];

    if (!readRegisters(
            imuAddress,
            REG_ACCEL_DATA,
            data,
            sizeof(data))) {
        Serial.println("读取六轴传感器数据失败");
        delay(100);
        return;
    }
    // 0x0B～0x0C：加速度计 XYZ 
    int16_t rawX = makeInt16(data[0], data[1]);
    int16_t rawY = makeInt16(data[2], data[3]);
    int16_t rawZ = makeInt16(data[4], data[5]);

    // 0x11～0x16：陀螺仪 XYZ 
    int16_t rawGyroX = makeInt16(data[6], data[7]); 
    int16_t rawGyroY = makeInt16(data[8], data[9]); 
    int16_t rawGyroZ = makeInt16(data[10], data[11]);

    /*
     * 当前配置是 ±2g。
     * 灵敏度为 16384 LSB/g。
     */
    float accelX = rawX / 16384.0f;
    float accelY = rawY / 16384.0f;
    float accelZ = rawZ / 16384.0f;
    /*
     * 当前配置是 ±250dps。
     * 灵敏度为 131 LSB/dps。
     */
    float gyroX = rawGyroX / 131.0f;
    float gyroY = rawGyroY / 131.0f;
    float gyroZ = rawGyroZ / 131.0f;

    Serial.printf(
        "X=%7.3f g  Y=%7.3f g  Z=%7.3f g  X=%7.3f dps  Y=%7.3f dps  Z=%7.3f dps\n",
        accelX,
        accelY,
        accelZ,
        gyroX,
        gyroY,
        gyroZ
    );

    delay(100);
}