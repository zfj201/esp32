#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// ============================================================
// 18_OV3660-camera
// CHD-ESP32-S3-BOX V2.0
// OV3660：单帧 /capture + MJPEG /stream
// ============================================================

// SCCB：配置摄像头寄存器
constexpr int CAM_SDA = 8;
constexpr int CAM_SCL = 18;

// ESP32-S3 给 OV3660 提供的主时钟
constexpr int CAM_XCLK = 39;

// DVP 同步信号
constexpr int CAM_PCLK  = 14;
constexpr int CAM_HREF  = 41;
constexpr int CAM_VSYNC = 42;

// ------------------------------------------------------------
// OV3660 使用 D[9:2] 作为 8-bit DVP 数据
//
// esp_camera 的 D0
// 并不是 OV3660 芯片物理意义上的 D0。
//
// esp_camera D0 = OV3660 D2
// esp_camera D7 = OV3660 D9
// ------------------------------------------------------------

constexpr int CAM_D0 = 12;  // OV3660 D2 / DVP_Y2
constexpr int CAM_D1 = 10;  // OV3660 D3 / DVP_Y3
constexpr int CAM_D2 = 9;   // OV3660 D4 / DVP_Y4
constexpr int CAM_D3 = 11;  // OV3660 D5 / DVP_Y5

constexpr int CAM_D4 = 13;  // OV3660 D6 / DVP_Y6
constexpr int CAM_D5 = 21;  // OV3660 D7 / DVP_Y7
constexpr int CAM_D6 = 38;  // OV3660 D8 / DVP_Y8
constexpr int CAM_D7 = 40;  // OV3660 D9 / DVP_Y9

constexpr char WIFI_SSID[] = "2801";
constexpr char WIFI_PASSWORD[] = "18367168360";

WebServer server(80);


bool initializeCamera()
{
    camera_config_t config = {};

    // --------------------------------------------------------
    // 1. 电源控制
    // --------------------------------------------------------
    // 本开发板没有把 RESET / PWDN 接给 ESP32 GPIO。
    config.pin_pwdn  = -1;
    config.pin_reset = -1;

    // --------------------------------------------------------
    // 2. XCLK
    // --------------------------------------------------------
    config.pin_xclk = CAM_XCLK;

    // --------------------------------------------------------
    // 3. SCCB
    // --------------------------------------------------------
    config.pin_sccb_sda = CAM_SDA;
    config.pin_sccb_scl = CAM_SCL;

    // --------------------------------------------------------
    // 4. DVP 8-bit 数据总线
    // --------------------------------------------------------
    config.pin_d0 = CAM_D0;
    config.pin_d1 = CAM_D1;
    config.pin_d2 = CAM_D2;
    config.pin_d3 = CAM_D3;
    config.pin_d4 = CAM_D4;
    config.pin_d5 = CAM_D5;
    config.pin_d6 = CAM_D6;
    config.pin_d7 = CAM_D7;

    // --------------------------------------------------------
    // 5. DVP 时序信号
    // --------------------------------------------------------
    config.pin_vsync = CAM_VSYNC;
    config.pin_href  = CAM_HREF;
    config.pin_pclk  = CAM_PCLK;

    // --------------------------------------------------------
    // 6. 给 OV3660 提供 20 MHz XCLK
    // --------------------------------------------------------
    config.xclk_freq_hz = 20000000;

    config.ledc_timer   = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;

    // --------------------------------------------------------
    // 7. 先让 OV3660 输出 JPEG
    // --------------------------------------------------------
    config.pixel_format = PIXFORMAT_JPEG;

    bool hasPsram = psramFound();

    // framebuffer：每一帧 JPEG 占用一块内存。
    // fb_count：同时准备几块 framebuffer。
    //   1 = 拍完必须立刻取走，否则下一帧等着（适合单次 /capture）
    //   2 = 双缓冲：一边 WiFi 发送当前帧，DMA 同时填下一块（适合 /stream）
    if (hasPsram)
    {
        config.frame_size  = FRAMESIZE_VGA;   // 640 × 480
        config.fb_count    = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        // 网络慢时丢掉旧帧，始终取最新画面，降低延迟。
        config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
        config.frame_size  = FRAMESIZE_QVGA;  // 320 × 240
        config.fb_count    = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        // fb_count=1 时只能 WHEN_EMPTY：等这块缓冲空出来再拍。
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    // JPEG：数字越小，质量越高、文件越大，FPS 往往越低。
    config.jpeg_quality = 12;

    // ========================================================
    // 真正初始化摄像头
    // ========================================================

    esp_err_t result = esp_camera_init(&config);

    if (result != ESP_OK)
    {
        Serial.printf(
            "Camera init failed: 0x%X (%s)\n",
            result,
            esp_err_to_name(result)
        );

        return false;
    }

    Serial.println("Camera init OK.");

    // ========================================================
    // 获取 sensor 对象
    // ========================================================

    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor == nullptr)
    {
        Serial.println("Cannot get camera sensor.");
        return false;
    }

    Serial.printf(
        "Sensor PID = 0x%04X\n",
        static_cast<unsigned int>(sensor->id.PID)
    );
    Serial.printf(
        "fb_count=%d  grab_mode=%s  jpeg_quality=%d\n",
        config.fb_count,
        config.grab_mode == CAMERA_GRAB_LATEST ? "LATEST" : "WHEN_EMPTY",
        config.jpeg_quality
    );

    return true;
}


void captureOneFrame()
{
    Serial.println();
    Serial.println("Capturing frame...");

    // ========================================================
    // 等待一整帧图像
    // ========================================================

    camera_fb_t *frame = esp_camera_fb_get();

    if (frame == nullptr)
    {
        Serial.println("Capture failed.");
        return;
    }

    // ========================================================
    // frame 现在就是这一张图片
    // ========================================================

    Serial.println("Capture OK.");

    Serial.printf(
        "Resolution: %u x %u\n",
        static_cast<unsigned int>(frame->width),
        static_cast<unsigned int>(frame->height)
    );

    Serial.printf(
        "Image size: %zu bytes\n",
        frame->len
    );

    // ========================================================
    // JPEG 文件正常应该以 FF D8 开头
    // ========================================================

    if (frame->len >= 2)
    {
        Serial.printf(
            "First bytes: %02X %02X\n",
            frame->buf[0],
            frame->buf[1]
        );

        if (
            frame->buf[0] == 0xFF &&
            frame->buf[1] == 0xD8
        )
        {
            Serial.println("JPEG SOI detected.");
        }
    }

    // ========================================================
    // 用完必须归还 framebuffer
    // ========================================================

    esp_camera_fb_return(frame);

    Serial.println("Frame buffer returned.");
}

bool initializeWifi()
{
    Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long timeoutMs = 15000;
    const unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        if (millis() - startMs >= timeoutMs)
        {
            Serial.println(" timeout");
            return false;
        }
    }

    Serial.println();
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    return true;
}

void handleCapture()
{
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr)
    {
        server.send(503, "text/plain", "Capture failed");
        return;
    }

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.setContentLength(frame->len);
    server.send(200, "image/jpeg", "");

    WiFiClient client = server.client();
    client.write(frame->buf, frame->len);

    esp_camera_fb_return(frame);
}

// MJPEG：一条 HTTP 连接里连续推多张 JPEG。
// 浏览器用 multipart/x-mixed-replace 不断替换 <img>，看起来就是视频。
// FPS = 每秒成功发出的 JPEG 张数，受 拍帧 / 压缩 / WiFi 发送 三者里最慢的一项限制。
void handleStream()
{
    WiFiClient client = server.client();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-cache, no-store");
    client.println("Pragma: no-cache");
    client.println("Access-Control-Allow-Origin: *");
    client.println();

    uint32_t lastFpsMs = millis();
    uint32_t framesThisSecond = 0;
    size_t lastJpegBytes = 0;

    while (client.connected())
    {
        // 取出一块已填好的 framebuffer；用完必须 fb_return，否则 fb_count 块会占满。
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == nullptr)
        {
            continue;
        }

        lastJpegBytes = frame->len;
        client.printf(
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            static_cast<unsigned int>(frame->len));

        size_t written = 0;
        while (written < frame->len && client.connected())
        {
            const size_t n = client.write(frame->buf + written, frame->len - written);
            if (n == 0)
            {
                break;
            }
            written += n;
        }
        client.print("\r\n");

        // 归还后驱动才能复用这块内存去拍下一帧（fb_count=2 时另一块可能已经在填）。
        esp_camera_fb_return(frame);
        framesThisSecond++;

        const uint32_t now = millis();
        if (now - lastFpsMs >= 1000)
        {
            Serial.printf(
                "MJPEG: %u FPS, last JPEG %u bytes\n",
                static_cast<unsigned int>(framesThisSecond),
                static_cast<unsigned int>(lastJpegBytes));
            framesThisSecond = 0;
            lastFpsMs = now;
        }
    }

    Serial.println("MJPEG client disconnected");
}

void handleRoot()
{
    server.send(
        200,
        "text/html",
        "<!DOCTYPE html><html><body>"
        "<h1>OV3660 MJPEG</h1>"
        "<p><a href=\"/stream\">/stream</a> live &nbsp; "
        "<a href=\"/capture\">/capture</a> still</p>"
        "<img src=\"/stream\" alt=\"stream\" />"
        "</body></html>");
}

void startHttpServer()
{
    server.on("/", handleRoot);
    server.on("/capture", handleCapture);
    server.on("/stream", handleStream);
    server.begin();
    Serial.print("Open http://");
    Serial.print(WiFi.localIP());
    Serial.println("/  or  /stream");
}


void setup()
{
    Serial.begin(115200);

    delay(1500);

    Serial.println();
    Serial.println("============================");
    Serial.println("OV3660 Camera Test");
    Serial.println("============================");

    Serial.printf(
        "PSRAM: %s\n",
        psramFound() ? "YES" : "NO"
    );

    if (!initializeCamera())
    {
        Serial.println("Camera initialization stopped.");
        return;
    }

    delay(500);

    captureOneFrame();

    if (!initializeWifi())
    {
        Serial.println("WiFi connection stopped.");
        return;
    }

    startHttpServer();
}


void loop()
{
    server.handleClient();
}
