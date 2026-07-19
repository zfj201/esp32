#include <Arduino.h>
#include <Wire.h>

static const int SDA_PIN = 8;
static const int SCL_PIN = 18;

static void scanI2C() {
  uint8_t found = 0;

  Serial.println();
  Serial.println("Scanning I2C bus...");

  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
      found++;
    } else if (error == 4) {
      Serial.printf("  Unknown error at 0x%02X\n", addr);
    }
  }

  if (found == 0) {
    Serial.println("  No I2C devices found");
  } else {
    Serial.printf("Done. %u device(s) found.\n", found);
  }
}

bool i2cDeviceExists(uint8_t addr) {
  Wire.beginTransmission(addr);
  const uint8_t error = Wire.endTransmission();
  return error == 0;
}

bool readRegister(uint8_t addr, uint8_t reg, uint8_t *value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  uint8_t error = Wire.endTransmission(false);
  if (error != 0) {
    return false;
  }
  size_t count = Wire.requestFrom(addr, (uint8_t)1);
  if (count != 1) {
    return false;
  }
  *value = Wire.read();
  return true;
}

bool writeRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    return false;
  }
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

void testICM() {
  const uint8_t candidateAddresses[] = {0x68, 0x69}; 
  for (uint8_t address : candidateAddresses) { 
    uint8_t whoAmI = 0; 
    if (readRegister(address, 0x75, &whoAmI)) { 
      Serial.printf( "地址 0x%02X，WHO_AM_I = 0x%02X\n", address, whoAmI ); 
      if (whoAmI == 0x60) { Serial.println("确认找到 ICM42607"); } 
    } 
  }
}
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("01_I2C-scan");
  if (Wire.begin(SDA_PIN, SCL_PIN)) {
    Serial.println("I2C initialized");
  } else {
    Serial.println("I2C initialization failed");
  }
  Wire.setTimeOut(500);
  // scanI2C();
}

void loop() {
  // Serial.println("loop");
  scanI2C();
  if (i2cDeviceExists(0x18)) {
    Serial.println("Device found at 0x18");
  }
  if (i2cDeviceExists(0x28)) {
    Serial.println("Device found at 0x28");
  }
  testICM();
  delay(5000);
}
