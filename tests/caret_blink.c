#include <assert.h>
#include <stdio.h>
#include "../caret_blink.h"

static bool tick(xr_caret_blink *s, double time, unsigned long long frame) {
    return xr_caret_visible(s, time, frame, 10, 20, 300, 80, 0, 0, -1, false);
}

int main(void) {
    xr_caret_blink s = { .restart = true };
    assert(tick(&s, 10.0, 1));
    assert(tick(&s, 10.49, 2));
    assert(!tick(&s, 10.51, 3));
    assert(!tick(&s, 10.99, 4));
    assert(tick(&s, 11.01, 5));
    assert(!tick(&s, 11.51, 6));
    // Clicking/typing while hidden immediately restarts the visible phase.
    assert(xr_caret_visible(&s, 11.6, 7, 10, 20, 300, 80, 0, 0, -1, true));
    assert(tick(&s, 12.09, 8));
    assert(!tick(&s, 12.11, 9));
    // A frame without focus, another field, cursor moves and programmatic
    // completion each begin a new visible phase.
    assert(tick(&s, 12.2, 11));
    assert(xr_caret_visible(&s, 12.8, 12, 20, 20, 300, 80, 0, 0, -1, false));
    assert(xr_caret_visible(&s, 13.4, 13, 20, 20, 300, 80, 1, 1, -1, false));
    assert(xr_caret_visible(&s, 14.0, 14, 20, 20, 300, 80, 1, 1, 0, false));
    s.restart = true;
    assert(xr_caret_visible(&s, 14.6, 15, 20, 20, 300, 80, 1, 1, 0, false));
    // Reinitializing a window may reset raylib's clock.
    assert(xr_caret_visible(&s, 0.1, 16, 20, 20, 300, 80, 1, 1, 0, false));
    puts("PASS: caret blink cycle and interaction/focus resets");
    return 0;
}
