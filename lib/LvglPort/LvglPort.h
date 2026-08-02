#pragma once

// 初始化 LCD、GT911 和 LVGL 的显示/输入适配层。
// LCD/LVGL 初始化成功但 GT911 初始化失败时返回 false，界面仍可显示错误信息。
bool lvglPortBegin();

// 在 Arduino loop() 中持续调用：处理 GT911 事件并运行 LVGL 定时器。
void lvglPortTask();
