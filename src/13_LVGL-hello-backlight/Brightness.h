#pragma once

#include <cstdint>

inline uint8_t brightnessPercentToDuty(uint8_t percent)
{
    const uint8_t clampedPercent = percent < 10
        ? 10
        : (percent > 100 ? 100 : percent);

    // 8-bit PWM 的范围是 0～255；加 50 后实现四舍五入。
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(clampedPercent) * 255U + 50U) / 100U
    );
}
