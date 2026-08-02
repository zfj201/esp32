#pragma once

#include <cstdint>

class LvglTouchTracker
{
public:
    bool handlePressed(uint8_t id, int32_t x, int32_t y);
    bool handleMoved(uint8_t id, int32_t x, int32_t y);
    bool handleReleased(uint8_t id);

    bool isPressed() const;
    int32_t x() const;
    int32_t y() const;

private:
    bool active_ = false;
    uint8_t activeId_ = 0;
    int32_t x_ = 0;
    int32_t y_ = 0;
};
