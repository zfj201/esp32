#include <Arduino.h>
#include <Wire.h>

// I2C_SDA / I2C_SCL 已由 esp32s3box 的 pins_arduino.h 定义为 8 / 18

constexpr uint8_t REG_WHO_AM_I    = 0x75;
constexpr uint8_t REG_PWR_MGMT0   = 0x1F;
constexpr uint8_t REG_ACCEL_CFG0  = 0x21;
constexpr uint8_t REG_ACCEL_DATA  = 0x0B;

constexpr uint8_t ICM42607_ID = 0x60;

uint8_t imuAddress = 0;

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

bool initAccelerometer()
{
    /*
     * ACCEL_CONFIG0 = 0x69
     *
     * bit 6:5 = 11：量程 ±2g
     * bit 3:0 = 1001：输出速率 100Hz
     */
    if (!writeRegister8(imuAddress, REG_ACCEL_CFG0, 0x69)) {
        return false;
    }

    /*
     * PWR_MGMT0 = 0x03
     *
     * GYRO_MODE  = 00：陀螺仪关闭
     * ACCEL_MODE = 11：加速度计低噪声模式
     */
    if (!writeRegister8(imuAddress, REG_PWR_MGMT0, 0x03)) {
        return false;
    }

    // 数据手册规定启动后需要等待有效数据
    delay(20);

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

    if (!initAccelerometer()) {
        Serial.println("加速度计初始化失败");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("加速度计初始化成功");
}

void loop()
{
    uint8_t data[6];

    if (!readRegisters(
            imuAddress,
            REG_ACCEL_DATA,
            data,
            sizeof(data))) {
        Serial.println("读取加速度失败");
        delay(100);
        return;
    }

    int16_t rawX = makeInt16(data[0], data[1]);
    int16_t rawY = makeInt16(data[2], data[3]);
    int16_t rawZ = makeInt16(data[4], data[5]);

    /*
     * 当前配置是 ±2g。
     * 灵敏度为 16384 LSB/g。
     */
    float accelX = rawX / 16384.0f;
    float accelY = rawY / 16384.0f;
    float accelZ = rawZ / 16384.0f;

    Serial.printf(
        "X=%7.3f g  Y=%7.3f g  Z=%7.3f g\n",
        accelX,
        accelY,
        accelZ
    );

    delay(100);
}