#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <image_fullscreen.h>

constexpr int SD_CLK = 43;
constexpr int SD_CMD = 0;
constexpr int SD_D0 = 44;

constexpr char MOUNT_POINT[] = "/sdcard";
constexpr char TEST_FILE[] = "/sdmmc_test.txt";
constexpr char TEST_CSV_FILE[] = "/sdmmc_test.csv";
constexpr char PHOTO_DIR[] = "/photo";
constexpr char IMAGE_FILE[] = "/photo/test.rgb565";

constexpr char TEST_MESSAGE[] = "ESP32-S3 SDMMC read/write OK";
constexpr char CSV_CONTENT[] =
  "time,temp\n"
  "10:01,25.6\n"
  "10:02,25.8\n";

bool initializeSdCard() {
  Serial.printf(
    "1-bit SDMMC pins: CLK=%d CMD=%d D0=%d\n",
    SD_CLK,
    SD_CMD,
    SD_D0
  );

  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) {
    Serial.println("FAIL: invalid SDMMC pin configuration");
    return false;
  }

  // true 表示 1-bit 模式，只使用 D0 数据线。
  if (!SD_MMC.begin(MOUNT_POINT, true)) {
    Serial.println("FAIL: SD card mount failed");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("FAIL: no SD card detected");
    SD_MMC.end();
    return false;
  }

  return true;
}

bool testTextFile() {
  File file = SD_MMC.open(TEST_FILE, FILE_WRITE);
  if (!file) {
    Serial.printf("FAIL: cannot open %s for write\n", TEST_FILE);
    return false;
  }

  const size_t written = file.println(TEST_MESSAGE);
  file.close();

  if (written == 0) {
    Serial.printf("FAIL: cannot write %s\n", TEST_FILE);
    return false;
  }

  file = SD_MMC.open(TEST_FILE, FILE_READ);
  if (!file) {
    Serial.printf("FAIL: cannot open %s for read\n", TEST_FILE);
    return false;
  }

  String content = file.readStringUntil('\n');
  file.close();
  content.trim();

  Serial.printf("Read back: %s\n", content.c_str());
  if (content != TEST_MESSAGE) {
    Serial.println("FAIL: read-back data does not match");
    return false;
  }

  return true;
}

bool writeAndPrintCsv() {
  File file = SD_MMC.open(TEST_CSV_FILE, FILE_WRITE);
  if (!file) {
    Serial.printf("FAIL: cannot open %s for write\n", TEST_CSV_FILE);
    return false;
  }

  const size_t expected = strlen(CSV_CONTENT);
  const size_t written = file.print(CSV_CONTENT);
  file.close();

  if (written != expected) {
    Serial.printf(
      "FAIL: CSV written %u / %u bytes\n",
      static_cast<unsigned>(written),
      static_cast<unsigned>(expected)
    );
    return false;
  }

  file = SD_MMC.open(TEST_CSV_FILE, FILE_READ);
  if (!file) {
    Serial.printf("FAIL: cannot open %s for read\n", TEST_CSV_FILE);
    return false;
  }

  Serial.println("CSV content:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println();

  return true;
}

bool writeRgb565Image() {
  if (!SD_MMC.exists(PHOTO_DIR) && !SD_MMC.mkdir(PHOTO_DIR)) {
    Serial.printf("FAIL: cannot create directory %s\n", PHOTO_DIR);
    return false;
  }

  File file = SD_MMC.open(IMAGE_FILE, FILE_WRITE);
  if (!file) {
    Serial.printf("FAIL: cannot open %s for write\n", IMAGE_FILE);
    return false;
  }

  const size_t expected = sizeof(IMAGE_FULLSCREEN);
  const size_t written = file.write(
    reinterpret_cast<const uint8_t*>(IMAGE_FULLSCREEN),
    expected
  );
  file.close();

  Serial.printf(
    "Image written: %u / %u bytes\n",
    static_cast<unsigned>(written),
    static_cast<unsigned>(expected)
  );
  return written == expected;
}

String joinPath(const char* directory, const char* name) {
  String path = directory;
  if (!path.endsWith("/")) {
    path += '/';
  }
  path += name;
  return path;
}

void printIndent(uint8_t depth) {
  for (uint8_t i = 0; i < depth; ++i) {
    Serial.print("  ");
  }
}

void listDirectory(fs::FS& fs, const char* directory, uint8_t depth = 0) {
  File root = fs.open(directory);
  if (!root || !root.isDirectory()) {
    Serial.printf("FAIL: cannot open directory %s\n", directory);
    return;
  }

  File entry = root.openNextFile();
  while (entry) {
    const String path = joinPath(directory, entry.name());
    const bool isDirectory = entry.isDirectory();
    const size_t size = entry.size();
    entry.close();

    printIndent(depth);
    if (isDirectory) {
      Serial.printf("DIR : %s\n", path.c_str());
      listDirectory(fs, path.c_str(), depth + 1);
    } else {
      Serial.printf(
        "FILE: %s  %u bytes\n",
        path.c_str(),
        static_cast<unsigned>(size)
      );
    }

    entry = root.openNextFile();
  }

  root.close();
}

void setup() {
  Serial.begin(115200);
  delay(5000);

  Serial.println("\n14_SDMMC-TF-test");
  if (!initializeSdCard()) {
    return;
  }

  bool passed = true;
  passed = testTextFile() && passed;
  passed = writeAndPrintCsv() && passed;
  passed = writeRgb565Image() && passed;

  Serial.println("\nSD card contents:");
  listDirectory(SD_MMC, "/");

  Serial.println(
    passed
      ? "\nPASS: all SD card tests succeeded."
      : "\nFAIL: one or more SD card tests failed."
  );
}

void loop() {
  delay(1000);
}
