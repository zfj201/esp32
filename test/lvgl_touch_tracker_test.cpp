#include <cassert>
#include <cstdint>

#include "../lib/LvglPort/LvglTouchTracker.h"

int main()
{
    LvglTouchTracker tracker;

    assert(!tracker.isPressed());

    assert(tracker.handlePressed(2, 10, 20));
    assert(tracker.isPressed());
    assert(tracker.x() == 10);
    assert(tracker.y() == 20);

    // A second finger must not steal the active pointer.
    assert(!tracker.handlePressed(3, 90, 100));
    assert(!tracker.handleMoved(3, 91, 101));
    assert(tracker.x() == 10);
    assert(tracker.y() == 20);

    assert(tracker.handleMoved(2, 30, 40));
    assert(tracker.x() == 30);
    assert(tracker.y() == 40);

    // Releasing another finger must not release the active pointer.
    assert(!tracker.handleReleased(3));
    assert(tracker.isPressed());

    assert(tracker.handleReleased(2));
    assert(!tracker.isPressed());
    assert(tracker.x() == 30);
    assert(tracker.y() == 40);

    // After release, another finger can become the active pointer.
    assert(tracker.handlePressed(3, 50, 60));
    assert(tracker.isPressed());
    assert(tracker.x() == 50);
    assert(tracker.y() == 60);

    return 0;
}
