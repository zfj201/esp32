#include <Arduino.h>
#include <Wire.h>
#include <cmath>
#include <driver/i2s.h>

// ============================================================
// 15_I2S-audio-tone
//
// 通过 ESP32-S3 I2S（主模式 TX）向 ES8311 DAC 输出 1 kHz 正弦波，
// 验证 CHD-ESP32-S3-BOX V2.0 板上的音频通路：
//   I2C 配置 ES8311 → I2S 送出 PCM → PA 放大 → 喇叭发声
// ============================================================

// ------------------------------------------------------------
// 硬件引脚（CHD-ESP32-S3-BOX V2.0 原理图）
// ------------------------------------------------------------

// ES8311 控制总线（I2C）
constexpr int AUDIO_I2C_SDA = 8;
constexpr int AUDIO_I2C_SCL = 18;

// I2S 数字音频总线：ESP32-S3 为主机，ES8311 为从机
constexpr int AUDIO_I2S_MCLK = 2;   // 主时钟，供 Codec 内部 PLL/分频
constexpr int AUDIO_I2S_BCLK = 17;  // 位时钟
constexpr int AUDIO_I2S_LRCLK = 45; // 左右声道选择（字时钟）
constexpr int AUDIO_I2S_DOUT = 15;  // ESP32 → ES8311 DAC 数据
constexpr int AUDIO_I2S_DIN = I2S_PIN_NO_CHANGE; // 本例不录音，不接 ADC

// 功放使能：HIGH 打开喇叭通路，LOW 静音并关断 PA
constexpr int AUDIO_PA_CTRL = 46;

// ES8311 7-bit I2C 地址（CE 引脚接地时为 0x18）
constexpr uint8_t ES8311_ADDRESS = 0x18;

// ------------------------------------------------------------
// 音频参数
// ------------------------------------------------------------

constexpr uint32_t SAMPLE_RATE = 48000;              // PCM 采样率 (Hz)
constexpr uint32_t MCLK_FREQUENCY = SAMPLE_RATE * 256; // 12.288 MHz，常见 256×fs
constexpr float TONE_FREQUENCY = 1000.0f;           // 测试音频率 (Hz)
constexpr int16_t TONE_AMPLITUDE = 5000;            // 峰值幅度（int16 满幅为 32767）
constexpr size_t FRAMES_PER_BUFFER = 240;           // 每缓冲帧数（约 5 ms @ 48 kHz）
constexpr float AUDIO_TWO_PI = 6.28318530717958647692f;

// 每个采样点的相位步进：2π × f / fs
constexpr float PHASE_INCREMENT =
    AUDIO_TWO_PI * TONE_FREQUENCY / static_cast<float>(SAMPLE_RATE);

// 交错立体声缓冲：L0,R0,L1,R1,... 共 FRAMES_PER_BUFFER 帧
int16_t toneBuffer[FRAMES_PER_BUFFER * 2];

// 单条 Codec 寄存器写入：地址 + 数值
struct CodecRegister {
  uint8_t address;
  uint8_t value;
};

// 致命错误：关 PA，打印原因后死循环，避免继续往喇叭灌数据
[[noreturn]] void fatalStop(const char *message) {
  digitalWrite(AUDIO_PA_CTRL, LOW);

  Serial.println();
  Serial.print("FATAL: ");
  Serial.println(message);

  while (true) {
    delay(1000);
  }
}

// 探测 ES8311 是否在总线上应答
bool es8311Probe() {
  Wire.beginTransmission(ES8311_ADDRESS);
  return Wire.endTransmission() == 0;
}

// 写单个寄存器；失败时最多重试 3 次
bool es8311WriteRegister(uint8_t registerAddress, uint8_t value) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    Wire.beginTransmission(ES8311_ADDRESS);
    Wire.write(registerAddress);
    Wire.write(value);

    if (Wire.endTransmission() == 0) {
      return true;
    }

    delay(2);
  }

  Serial.printf(
      "ES8311 register write failed: reg=0x%02X value=0x%02X\n",
      registerAddress,
      value);
  return false;
}

// 按顺序写入一组寄存器；任一失败则中止
bool es8311WriteSequence(
    const CodecRegister *registers,
    size_t registerCount) {
  for (size_t index = 0; index < registerCount; ++index) {
    if (!es8311WriteRegister(
            registers[index].address,
            registers[index].value)) {
      return false;
    }
  }

  return true;
}

// 配置 ES8311：48 kHz、16-bit I2S、12.288 MHz MCLK、从机、仅开 DAC。
// 寄存器顺序依据 Espressif ES8311 驱动与芯片用户手册。
bool initializeEs8311() {
  static constexpr CodecRegister initializationSequence[] = {
      // ---- 上电 / I2C 稳定性 ----
      {0x44, 0x08},  // 提高 I2C 抗干扰能力
      {0x44, 0x08},  // 首次写入偶发失败，按官方驱动重复一次
      {0x01, 0x30},  // 时钟管理：上电准备
      {0x02, 0x00},  // 时钟分频预置
      {0x03, 0x10},  // ADC 时钟相关预置
      {0x16, 0x24},  // 模拟电源相关
      {0x04, 0x10},  // DAC 时钟预置
      {0x05, 0x00},  // 系统时钟分频预置
      {0x0B, 0x00},  // 系统控制
      {0x0C, 0x00},  // 系统控制
      {0x10, 0x1F},  // 系统时序
      {0x11, 0x7F},  // 系统时序

      // ---- 时钟与从机模式（依赖 ESP32 提供的 MCLK/BCLK/LRCK）----
      {0x00, 0x80},  // 从机模式（不自产 BCLK/LRCK）
      {0x01, 0x3F},  // 使用 MCLK 引脚作为内部时钟源
      {0x02, 0x00},  // 系数：MCLK 12.288 MHz ↔ LRCK 48 kHz
      {0x03, 0x10},  // ADC 过采样相关
      {0x04, 0x10},  // DAC 过采样相关
      {0x05, 0x00},  // 分频系数高位
      {0x06, 0x03},  // BCLK = MCLK / 4 → 3.072 MHz
      {0x07, 0x00},  // LRCK 分频高字节
      {0x08, 0xFF},  // LRCK 分频低字节：比值 256（MCLK/LRCK）

      // ---- 数字接口格式 ----
      {0x09, 0x0C},  // DAC 侧：标准 I2S、16-bit
      {0x0A, 0x4C},  // ADC 侧：16-bit，本例不启用 ADC

      // ---- 模拟 / DAC 通路 ----
      {0x13, 0x10},  // 电源管理
      {0x1B, 0x0A},  // ADC 模拟（保持默认安全值）
      {0x1C, 0x6A},  // ADC 模拟（保持默认安全值）
      {0x44, 0x58},  // 使能内部 DAC 参考电压
      {0x17, 0xBF},  // ADC 相关控制（官方序列保留）
      {0x0E, 0x02},  // DAC 采样率相关
      {0x12, 0x00},  // 系统电源
      {0x14, 0x1A},  // 模拟偏置 / PGA
      {0x0D, 0x01},  // 系统启动
      {0x15, 0x40},  // ADC 增益相关
      {0x37, 0x08},  // DAC 输出驱动
      {0x45, 0x00},  // 模拟输出相关
      {0x31, 0x00},  // DAC 解除静音
      {0x32, 0xB0},  // DAC 数字音量（偏低，便于安全试听）
  };

  return es8311WriteSequence(
      initializationSequence,
      sizeof(initializationSequence) / sizeof(initializationSequence[0]));
}

// 安装 I2S0 为 Master TX，输出立体声 16-bit PCM，并固定 MCLK
bool initializeI2s() {
  i2s_config_t config = {};
  config.mode =
      static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX); // 主机 + 仅发送
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // 交错 L/R
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S; // 标准 Philips I2S
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;                 // DMA 缓冲个数
  config.dma_buf_len = FRAMES_PER_BUFFER;   // 每个 DMA 缓冲的帧数
  config.use_apll = true;                   // APLL 提高时钟精度，利于音频
  config.tx_desc_auto_clear = true;         // 欠载时自动清零，减轻噪声
  config.fixed_mclk = MCLK_FREQUENCY;       // 固定输出 12.288 MHz MCLK

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

  // 清空 DMA，避免上电瞬间残留脏数据进入 Codec
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

// 预生成一整块 1 kHz 正弦波（左右声道相同），供 loop 循环发送
void generateToneBuffer() {
  float phase = 0.0f;

  for (size_t frame = 0; frame < FRAMES_PER_BUFFER; ++frame) {
    const int16_t sample =
        static_cast<int16_t>(sinf(phase) * TONE_AMPLITUDE);

    toneBuffer[frame * 2] = sample;     // Left
    toneBuffer[frame * 2 + 1] = sample; // Right

    phase += PHASE_INCREMENT;
    if (phase >= AUDIO_TWO_PI) {
      phase -= AUDIO_TWO_PI;
    }
  }
}

void setup() {
  // 先关 PA，避免初始化过程中的杂音
  pinMode(AUDIO_PA_CTRL, OUTPUT);
  digitalWrite(AUDIO_PA_CTRL, LOW);

  Serial.begin(115200);
  delay(1500); // 等待 USB 串口就绪，便于查看启动日志

  Serial.println();
  Serial.println("15_I2S-audio-tone");
  Serial.printf(
      "I2S pins: MCLK=%d BCLK=%d LRCLK=%d DOUT=%d PA=%d\n",
      AUDIO_I2S_MCLK,
      AUDIO_I2S_BCLK,
      AUDIO_I2S_LRCLK,
      AUDIO_I2S_DOUT,
      AUDIO_PA_CTRL);

  // 1) I2C：配置 ES8311
  if (!Wire.begin(AUDIO_I2C_SDA, AUDIO_I2C_SCL, 400000)) {
    fatalStop("I2C initialization failed");
  }
  Wire.setTimeOut(50);

  if (!es8311Probe()) {
    fatalStop("ES8311 not found at I2C address 0x18");
  }
  Serial.println("ES8311 detected at 0x18");

  // 2) I2S：先起时钟再写 Codec，使 ES8311 能锁定 MCLK
  if (!initializeI2s()) {
    fatalStop("I2S initialization failed");
  }
  Serial.printf(
      "I2S started: %lu Hz, 16-bit stereo, MCLK=%lu Hz\n",
      static_cast<unsigned long>(SAMPLE_RATE),
      static_cast<unsigned long>(MCLK_FREQUENCY));

  // 3) ES8311 DAC 寄存器
  if (!initializeEs8311()) {
    fatalStop("ES8311 initialization failed");
  }
  Serial.println("ES8311 DAC initialized");

  // 4) 准备 PCM，打开功放后开始播放
  generateToneBuffer();
  digitalWrite(AUDIO_PA_CTRL, HIGH);
  Serial.println("PA enabled; playing a low-volume 1 kHz tone");
}

void loop() {
  // 阻塞写入同一段正弦缓冲，DMA 持续送往 ES8311
  size_t bytesWritten = 0;
  const esp_err_t error = i2s_write(
      I2S_NUM_0,
      toneBuffer,
      sizeof(toneBuffer),
      &bytesWritten,
      portMAX_DELAY);

  if (error != ESP_OK || bytesWritten != sizeof(toneBuffer)) {
    fatalStop("I2S audio write failed");
  }
}
