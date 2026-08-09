#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>

// ============================================================
// 17_I2S-audio-loopback
//
// 板载双麦克风 -> ES7210 ADC -> ESP32-S3 I2S RX
// -> 双麦混合/衰减 -> I2S TX -> ES8311 DAC -> 单声道功放
//
// 这是低延迟实时监听测试，不会把录音保存到 SD 卡。
// ============================================================

constexpr int AUDIO_I2C_SDA = 8;
constexpr int AUDIO_I2C_SCL = 18;

constexpr int AUDIO_I2S_MCLK = 2;
constexpr int AUDIO_I2S_BCLK = 17;
constexpr int AUDIO_I2S_LRCLK = 45;  // 沿用当前 CHD V2.0 音频模块配置
constexpr int AUDIO_I2S_DIN = 16;    // ES7210 -> ESP32-S3
constexpr int AUDIO_I2S_DOUT = 15;   // ESP32-S3 -> ES8311
constexpr int AUDIO_PA_CTRL = 46;

constexpr uint8_t ES7210_ADDRESS = 0x40;
constexpr uint8_t ES8311_ADDRESS = 0x18;

// 语音回放采用 16 kHz，可降低处理量并缩短缓冲延迟。
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t MCLK_FREQUENCY = SAMPLE_RATE * 256;
constexpr size_t FRAMES_PER_BUFFER = 128;  // 8 ms 音频
constexpr uint8_t OUTPUT_ATTENUATION_SHIFT = 1;  // 除以 2，比初版提高约 12 dB
constexpr uint8_t ES7210_MIC_GAIN = 6;  // 18 dB（每级 3 dB）

int16_t inputStereo[FRAMES_PER_BUFFER * 2];
int16_t outputStereo[FRAMES_PER_BUFFER * 2];
bool i2sReady = false;

struct CodecRegister {
  uint8_t address;
  uint8_t value;
};

[[noreturn]] void fatalStop(const char *message) {
  digitalWrite(AUDIO_PA_CTRL, LOW);
  if (i2sReady) {
    i2s_zero_dma_buffer(I2S_NUM_0);
  }

  Serial.println();
  Serial.print("FATAL: ");
  Serial.println(message);

  while (true) {
    delay(1000);
  }
}

bool codecProbe(uint8_t deviceAddress) {
  Wire.beginTransmission(deviceAddress);
  return Wire.endTransmission() == 0;
}

bool codecWriteRegister(
    uint8_t deviceAddress,
    uint8_t registerAddress,
    uint8_t value) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    Wire.beginTransmission(deviceAddress);
    Wire.write(registerAddress);
    Wire.write(value);
    if (Wire.endTransmission() == 0) {
      return true;
    }
    delay(2);
  }

  Serial.printf(
      "I2C write failed: device=0x%02X reg=0x%02X value=0x%02X\n",
      deviceAddress,
      registerAddress,
      value);
  return false;
}

bool codecReadRegister(
    uint8_t deviceAddress,
    uint8_t registerAddress,
    uint8_t &value) {
  Wire.beginTransmission(deviceAddress);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(deviceAddress, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool codecUpdateRegister(
    uint8_t deviceAddress,
    uint8_t registerAddress,
    uint8_t mask,
    uint8_t value) {
  uint8_t currentValue = 0;
  if (!codecReadRegister(deviceAddress, registerAddress, currentValue)) {
    Serial.printf(
        "I2C read failed: device=0x%02X reg=0x%02X\n",
        deviceAddress,
        registerAddress);
    return false;
  }

  const uint8_t updatedValue =
      (currentValue & static_cast<uint8_t>(~mask)) | (value & mask);
  return codecWriteRegister(
      deviceAddress,
      registerAddress,
      updatedValue);
}

bool codecWriteSequence(
    uint8_t deviceAddress,
    const CodecRegister *registers,
    size_t registerCount) {
  for (size_t index = 0; index < registerCount; ++index) {
    if (!codecWriteRegister(
            deviceAddress,
            registers[index].address,
            registers[index].value)) {
      return false;
    }
  }
  return true;
}

bool initializeI2s() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(
      I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 6;
  config.dma_buf_len = FRAMES_PER_BUFFER;
  config.use_apll = true;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = MCLK_FREQUENCY;

  const i2s_pin_config_t pins = {
      .mck_io_num = AUDIO_I2S_MCLK,
      .bck_io_num = AUDIO_I2S_BCLK,
      .ws_io_num = AUDIO_I2S_LRCLK,
      .data_out_num = AUDIO_I2S_DOUT,
      .data_in_num = AUDIO_I2S_DIN,
  };

  esp_err_t error = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  if (error != ESP_OK) {
    Serial.printf("i2s_driver_install failed: 0x%X\n", error);
    return false;
  }

  error = i2s_set_pin(I2S_NUM_0, &pins);
  if (error != ESP_OK) {
    Serial.printf("i2s_set_pin failed: 0x%X\n", error);
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

bool initializeEs7210() {
  // 初始化值与 Espressif ES7210 驱动一致；ADC 使用 I2S 从机模式。
  static constexpr CodecRegister initializationSequence[] = {
      {0x00, 0xFF},  // 芯片复位
      {0x00, 0x41},
      {0x01, 0x3F},  // 初始化期间关闭 ADC 时钟
      {0x09, 0x30},  // 芯片状态切换周期
      {0x0A, 0x30},  // 上电状态切换周期
      {0x23, 0x2A},  // ADC1/2 高通滤波器
      {0x22, 0x0A},
      {0x20, 0x0A},  // ADC3/4 高通滤波器
      {0x21, 0x2A},
      {0x40, 0x43},  // 模拟电源与 VMID
      {0x41, 0x70},  // MIC1/2 偏置 2.87 V
      {0x42, 0x70},  // MIC3/4 偏置 2.87 V
      {0x02, 0xC1},  // 4.096 MHz MCLK / 16 kHz 时钟系数
      {0x07, 0x20},  // ADC OSR
      {0x04, 0x01},  // LRCK 分频高字节
      {0x05, 0x00},  // LRCK 分频低字节：256
      {0x11, 0x60},  // 标准 I2S、16-bit
      {0x12, 0x00},  // 双麦模式，不启用 TDM
      {0x47, 0x08},  // MIC1 模拟通路上电
      {0x48, 0x08},  // MIC2 模拟通路上电
      {0x49, 0x08},
      {0x4A, 0x08},
      {0x4B, 0x00},  // MIC1/2 PGA、ADC 与偏置上电
      {0x4C, 0xFF},  // MIC3/4 保持关闭
      {0x43, static_cast<uint8_t>(0x10 | ES7210_MIC_GAIN)},
      {0x44, static_cast<uint8_t>(0x10 | ES7210_MIC_GAIN)},
      {0x45, 0x00},
      {0x46, 0x00},
      {0x06, 0x00},  // 退出 power-down
      {0x01, 0x34},  // 打开 MIC1/2 所需 ADC 时钟
  };

  if (!codecWriteSequence(
          ES7210_ADDRESS,
          initializationSequence,
          sizeof(initializationSequence) / sizeof(initializationSequence[0]))) {
    return false;
  }

  // bit0=0：ES7210 为从机，由 ESP32-S3 提供 BCLK/LRCK。
  return codecUpdateRegister(ES7210_ADDRESS, 0x08, 0x01, 0x00);
}

bool initializeEs8311() {
  // 16 kHz、MCLK=4.096 MHz 的分频字段来自 Espressif ES8311 系数表。
  static constexpr CodecRegister initializationSequence[] = {
      {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00},
      {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
      {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
      {0x00, 0x80}, {0x01, 0x3F}, {0x02, 0x00}, {0x03, 0x10},
      {0x04, 0x20}, {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00},
      {0x08, 0xFF}, {0x09, 0x0C}, {0x0A, 0x4C}, {0x13, 0x10},
      {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58}, {0x17, 0xBF},
      {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01},
      {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00}, {0x31, 0x00},
      {0x32, 0xB0},
  };

  return codecWriteSequence(
      ES8311_ADDRESS,
      initializationSequence,
      sizeof(initializationSequence) / sizeof(initializationSequence[0]));
}

void discardStartupAudio() {
  // 丢弃模拟通路刚上电时的瞬态数据，再开启功放。
  for (int block = 0; block < 4; ++block) {
    size_t bytesRead = 0;
    const esp_err_t error = i2s_read(
        I2S_NUM_0,
        inputStereo,
        sizeof(inputStereo),
        &bytesRead,
        pdMS_TO_TICKS(100));
    if (error != ESP_OK || bytesRead == 0) {
      fatalStop("No startup audio received from ES7210");
    }
  }
}

void writeAllToI2s(const uint8_t *data, size_t byteCount) {
  size_t totalBytesWritten = 0;
  while (totalBytesWritten < byteCount) {
    size_t bytesWritten = 0;
    const esp_err_t error = i2s_write(
        I2S_NUM_0,
        data + totalBytesWritten,
        byteCount - totalBytesWritten,
        &bytesWritten,
        portMAX_DELAY);
    if (error != ESP_OK || bytesWritten == 0) {
      fatalStop("I2S loopback write failed");
    }
    totalBytesWritten += bytesWritten;
  }
}

void processLoopbackBlock() {
  size_t bytesRead = 0;
  const esp_err_t error = i2s_read(
      I2S_NUM_0,
      inputStereo,
      sizeof(inputStereo),
      &bytesRead,
      portMAX_DELAY);
  if (error != ESP_OK || bytesRead == 0 ||
      (bytesRead % (2 * sizeof(int16_t))) != 0) {
    fatalStop("I2S loopback read failed");
  }

  const size_t frameCount = bytesRead / (2 * sizeof(int16_t));
  int32_t peak = 0;

  for (size_t frame = 0; frame < frameCount; ++frame) {
    const int32_t left = inputStereo[frame * 2];
    const int32_t right = inputStereo[frame * 2 + 1];
    const int32_t mixed = (left + right) / 2;
    const int16_t output = static_cast<int16_t>(
        mixed >> OUTPUT_ATTENUATION_SHIFT);

    outputStereo[frame * 2] = output;
    outputStereo[frame * 2 + 1] = output;

    const int32_t magnitude = output < 0 ? -output : output;
    if (magnitude > peak) {
      peak = magnitude;
    }
  }

  writeAllToI2s(
      reinterpret_cast<const uint8_t *>(outputStereo),
      frameCount * 2 * sizeof(int16_t));

  static uint32_t lastMeterTime = 0;
  static int32_t recentPeak = 0;
  if (peak > recentPeak) {
    recentPeak = peak;
  }
  const uint32_t now = millis();
  if (now - lastMeterTime >= 1000) {
    Serial.printf("Loopback peak: %ld / 32767\n", static_cast<long>(recentPeak));
    recentPeak = 0;
    lastMeterTime = now;
  }
}

void setup() {
  pinMode(AUDIO_PA_CTRL, OUTPUT);
  digitalWrite(AUDIO_PA_CTRL, LOW);

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("17_I2S-audio-loopback");
  Serial.printf(
      "I2S: MCLK=%d BCLK=%d LRCLK=%d DIN=%d DOUT=%d, %lu Hz\n",
      AUDIO_I2S_MCLK,
      AUDIO_I2S_BCLK,
      AUDIO_I2S_LRCLK,
      AUDIO_I2S_DIN,
      AUDIO_I2S_DOUT,
      static_cast<unsigned long>(SAMPLE_RATE));
  Serial.printf(
      "Safety attenuation: 1/%u; keep the microphone away from the speaker\n",
      1U << OUTPUT_ATTENUATION_SHIFT);

  if (!Wire.begin(AUDIO_I2C_SDA, AUDIO_I2C_SCL, 400000)) {
    fatalStop("I2C initialization failed");
  }
  Wire.setTimeOut(50);

  if (!codecProbe(ES7210_ADDRESS)) {
    fatalStop("ES7210 microphone ADC not found at I2C address 0x40");
  }
  if (!codecProbe(ES8311_ADDRESS)) {
    fatalStop("ES8311 speaker DAC not found at I2C address 0x18");
  }
  Serial.println("ES7210 and ES8311 detected");

  // 先输出共享时钟，再初始化两个依赖 MCLK/BCLK/LRCK 的 Codec。
  if (!initializeI2s()) {
    fatalStop("Full-duplex I2S initialization failed");
  }
  i2sReady = true;

  if (!initializeEs7210()) {
    fatalStop("ES7210 initialization failed");
  }
  if (!initializeEs8311()) {
    fatalStop("ES8311 initialization failed");
  }

  discardStartupAudio();
  digitalWrite(AUDIO_PA_CTRL, HIGH);
  Serial.println("PA enabled; real-time microphone loopback started");
}

void loop() {
  processLoopbackBlock();
}
