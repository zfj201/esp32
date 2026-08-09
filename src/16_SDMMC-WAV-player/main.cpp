#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <driver/i2s.h>

// ============================================================
// 16_SDMMC-WAV-player
//
// 从 TF 卡根目录循环播放 /月光.wav：
//   SDMMC 读取 WAV -> 单声道 PCM 复制到左右 I2S 槽位
//   -> ES8311 DAC -> 板载单声道功放/喇叭
//
// WAV 必须为：PCM、44100 Hz、16-bit、单声道。
// ============================================================

// TF 卡：1-bit SDMMC
constexpr int SD_CLK = 43;
constexpr int SD_CMD = 0;
constexpr int SD_D0 = 44;
constexpr char SD_MOUNT_POINT[] = "/sdcard";
constexpr char AUDIO_FILE_PATH[] = "/wav/月光.wav";

// ES8311 控制总线（I2C）
constexpr int AUDIO_I2C_SDA = 8;
constexpr int AUDIO_I2C_SCL = 18;

// ESP32-S3 -> ES8311 的 I2S 总线
constexpr int AUDIO_I2S_MCLK = 2;
constexpr int AUDIO_I2S_BCLK = 17;
constexpr int AUDIO_I2S_LRCLK = 45;
constexpr int AUDIO_I2S_DOUT = 15;
constexpr int AUDIO_I2S_DIN = I2S_PIN_NO_CHANGE;
constexpr int AUDIO_PA_CTRL = 46;

constexpr uint8_t ES8311_ADDRESS = 0x18;
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint32_t MCLK_FREQUENCY = SAMPLE_RATE * 256;

// 每次从 SD 卡读取 512 个单声道采样（约 11.6 ms）。
constexpr size_t FRAMES_PER_BUFFER = 512;
uint8_t monoBuffer[FRAMES_PER_BUFFER * sizeof(int16_t)];
int16_t stereoBuffer[FRAMES_PER_BUFFER * 2];

File audioFile;
uint32_t audioDataOffset = 0;
uint32_t audioDataSize = 0;
uint32_t audioBytesRemaining = 0;
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

  if (audioFile) {
    audioFile.close();
  }
  SD_MMC.end();

  while (true) {
    delay(1000);
  }
}

uint16_t readLittleEndian16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLittleEndian32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

bool readExactly(File &file, uint8_t *destination, size_t byteCount) {
  return file.read(destination, byteCount) == byteCount;
}

bool initializeSdCard() {
  Serial.printf(
      "SDMMC pins: CLK=%d CMD=%d D0=%d (1-bit mode)\n",
      SD_CLK,
      SD_CMD,
      SD_D0);

  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) {
    Serial.println("Invalid SDMMC pin configuration");
    return false;
  }

  if (!SD_MMC.begin(SD_MOUNT_POINT, true)) {
    Serial.println("SD card mount failed");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD card detected");
    SD_MMC.end();
    return false;
  }

  Serial.printf(
      "SD card mounted, size=%llu MB\n",
      static_cast<unsigned long long>(SD_MMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

// 遍历 RIFF chunk，不假定 data 紧跟在 44 字节标准头之后。
bool openAndValidateWav() {
  audioFile = SD_MMC.open(AUDIO_FILE_PATH, FILE_READ);
  if (!audioFile) {
    Serial.printf("Cannot open audio file: %s\n", AUDIO_FILE_PATH);
    return false;
  }

  uint8_t riffHeader[12];
  if (!readExactly(audioFile, riffHeader, sizeof(riffHeader)) ||
      memcmp(riffHeader, "RIFF", 4) != 0 ||
      memcmp(riffHeader + 8, "WAVE", 4) != 0) {
    Serial.println("Invalid WAV: missing RIFF/WAVE header");
    return false;
  }

  bool foundFormat = false;
  bool foundData = false;
  uint16_t audioFormat = 0;
  uint16_t channelCount = 0;
  uint32_t sampleRate = 0;
  uint32_t byteRate = 0;
  uint16_t blockAlign = 0;
  uint16_t bitsPerSample = 0;

  while (audioFile.position() + 8 <= audioFile.size()) {
    uint8_t chunkHeader[8];
    if (!readExactly(audioFile, chunkHeader, sizeof(chunkHeader))) {
      Serial.println("Invalid WAV: truncated chunk header");
      return false;
    }

    const uint32_t chunkSize = readLittleEndian32(chunkHeader + 4);
    const uint32_t chunkDataOffset = audioFile.position();
    const uint64_t nextChunkOffset =
        static_cast<uint64_t>(chunkDataOffset) + chunkSize + (chunkSize & 1U);

    if (nextChunkOffset > audioFile.size()) {
      Serial.println("Invalid WAV: chunk exceeds file size");
      return false;
    }

    if (memcmp(chunkHeader, "fmt ", 4) == 0) {
      if (chunkSize < 16) {
        Serial.println("Invalid WAV: fmt chunk is too short");
        return false;
      }

      uint8_t formatData[16];
      if (!readExactly(audioFile, formatData, sizeof(formatData))) {
        Serial.println("Invalid WAV: truncated fmt chunk");
        return false;
      }

      audioFormat = readLittleEndian16(formatData);
      channelCount = readLittleEndian16(formatData + 2);
      sampleRate = readLittleEndian32(formatData + 4);
      byteRate = readLittleEndian32(formatData + 8);
      blockAlign = readLittleEndian16(formatData + 12);
      bitsPerSample = readLittleEndian16(formatData + 14);
      foundFormat = true;
    } else if (memcmp(chunkHeader, "data", 4) == 0) {
      audioDataOffset = chunkDataOffset;
      audioDataSize = chunkSize;
      foundData = true;
    }

    if (!audioFile.seek(static_cast<uint32_t>(nextChunkOffset))) {
      Serial.println("Invalid WAV: cannot seek to next chunk");
      return false;
    }

    if (foundFormat && foundData) {
      break;
    }
  }

  if (!foundFormat || !foundData) {
    Serial.println("Invalid WAV: fmt or data chunk is missing");
    return false;
  }

  if (audioFormat != 1 || channelCount != 1 || sampleRate != SAMPLE_RATE ||
      bitsPerSample != 16 || blockAlign != 2 ||
      byteRate != SAMPLE_RATE * sizeof(int16_t)) {
    Serial.printf(
        "Unsupported WAV: format=%u channels=%u rate=%lu bits=%u "
        "blockAlign=%u byteRate=%lu\n",
        audioFormat,
        channelCount,
        static_cast<unsigned long>(sampleRate),
        bitsPerSample,
        blockAlign,
        static_cast<unsigned long>(byteRate));
    Serial.println("Required: PCM, mono, 44100 Hz, 16-bit");
    return false;
  }

  if (audioDataSize == 0 || (audioDataSize % sizeof(int16_t)) != 0) {
    Serial.println("Invalid WAV: PCM data size must be a non-zero even number");
    return false;
  }

  if (!audioFile.seek(audioDataOffset)) {
    Serial.println("Cannot seek to WAV audio data");
    return false;
  }
  audioBytesRemaining = audioDataSize;

  Serial.printf(
      "WAV ready: %s, %lu PCM bytes, %.1f seconds\n",
      AUDIO_FILE_PATH,
      static_cast<unsigned long>(audioDataSize),
      static_cast<double>(audioDataSize) /
          static_cast<double>(SAMPLE_RATE * sizeof(int16_t)));
  return true;
}

bool es8311Probe() {
  Wire.beginTransmission(ES8311_ADDRESS);
  return Wire.endTransmission() == 0;
}

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

// MCLK 始终为 256 * fs，因此 44.1 kHz 与测试模块使用相同分频比。
bool initializeEs8311() {
  static constexpr CodecRegister initializationSequence[] = {
      {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00},
      {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
      {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
      {0x00, 0x80}, {0x01, 0x3F}, {0x02, 0x00}, {0x03, 0x10},
      {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00},
      {0x08, 0xFF}, {0x09, 0x0C}, {0x0A, 0x4C}, {0x13, 0x10},
      {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x58}, {0x17, 0xBF},
      {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01},
      {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00}, {0x31, 0x00},
      {0x32, 0xB0},
  };

  return es8311WriteSequence(
      initializationSequence,
      sizeof(initializationSequence) / sizeof(initializationSequence[0]));
}

bool initializeI2s() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
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

void rewindAudio() {
  if (!audioFile.seek(audioDataOffset)) {
    fatalStop("Cannot rewind WAV audio data");
  }
  audioBytesRemaining = audioDataSize;
  Serial.println("WAV finished; restarting from the beginning");
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
      fatalStop("I2S audio write failed");
    }
    totalBytesWritten += bytesWritten;
  }
}

void streamNextAudioBlock() {
  if (audioBytesRemaining == 0) {
    rewindAudio();
  }

  const size_t requestedBytes = min(
      static_cast<size_t>(audioBytesRemaining),
      sizeof(monoBuffer));
  const size_t bytesRead = audioFile.read(monoBuffer, requestedBytes);
  if (bytesRead != requestedBytes || (bytesRead % sizeof(int16_t)) != 0) {
    fatalStop("Unexpected end of WAV audio data");
  }

  const size_t frameCount = bytesRead / sizeof(int16_t);
  for (size_t frame = 0; frame < frameCount; ++frame) {
    const int16_t sample = static_cast<int16_t>(
        readLittleEndian16(monoBuffer + frame * sizeof(int16_t)));
    stereoBuffer[frame * 2] = sample;
    stereoBuffer[frame * 2 + 1] = sample;
  }

  writeAllToI2s(
      reinterpret_cast<const uint8_t *>(stereoBuffer),
      frameCount * 2 * sizeof(int16_t));
  audioBytesRemaining -= bytesRead;
}

void setup() {
  pinMode(AUDIO_PA_CTRL, OUTPUT);
  digitalWrite(AUDIO_PA_CTRL, LOW);

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("16_SDMMC-WAV-player");

  if (!initializeSdCard()) {
    fatalStop("SD card initialization failed");
  }
  if (!openAndValidateWav()) {
    fatalStop("WAV file validation failed");
  }

  if (!Wire.begin(AUDIO_I2C_SDA, AUDIO_I2C_SCL, 400000)) {
    fatalStop("I2C initialization failed");
  }
  Wire.setTimeOut(50);
  if (!es8311Probe()) {
    fatalStop("ES8311 not found at I2C address 0x18");
  }

  // 先启动 I2S/MCLK，再配置依赖外部主时钟的 ES8311。
  if (!initializeI2s()) {
    fatalStop("I2S initialization failed");
  }
  i2sReady = true;
  if (!initializeEs8311()) {
    fatalStop("ES8311 initialization failed");
  }

  digitalWrite(AUDIO_PA_CTRL, HIGH);
  Serial.printf(
      "PA enabled; looping %s at %lu Hz\n",
      AUDIO_FILE_PATH,
      static_cast<unsigned long>(SAMPLE_RATE));
}

void loop() {
  streamNextAudioBlock();
}
