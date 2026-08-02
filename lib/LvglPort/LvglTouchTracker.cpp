#include "LvglTouchTracker.h"

bool LvglTouchTracker::handlePressed(
    uint8_t id,
    int32_t x,
    int32_t y)
{
    if (active_)
    {
        return false;
    }

    active_ = true;
    activeId_ = id;
    x_ = x;
    y_ = y;
    return true;
}

bool LvglTouchTracker::handleMoved(
    uint8_t id,
    int32_t x,
    int32_t y)
{
    if (!active_ || id != activeId_)
    {
        return false;
    }

    x_ = x;
    y_ = y;
    return true;
}

bool LvglTouchTracker::handleReleased(uint8_t id)
{
    if (!active_ || id != activeId_)
    {
        return false;
    }

    active_ = false;
    return true;
}

bool LvglTouchTracker::isPressed() const
{
    return active_;
}

int32_t LvglTouchTracker::x() const
{
    return x_;
}

int32_t LvglTouchTracker::y() const
{
    return y_;
}
