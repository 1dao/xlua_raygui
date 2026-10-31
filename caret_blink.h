#ifndef XR_CARET_BLINK_H
#define XR_CARET_BLINK_H

#include <stdbool.h>
#include <math.h>

typedef struct {
    double started;
    unsigned long long frame;
    float x, y, width, height;
    int cursor, length, selection;
    bool restart;
} xr_caret_blink;

// Only the focused control calls this function. Unfocused controls must not
// reset another field's timer when a form draws several textboxes per frame.
static bool xr_caret_visible(xr_caret_blink *state, double now,
    unsigned long long frame, float x, float y, float width, float height,
    int cursor, int length, int selection, bool activity) {
    bool reset = state->restart || now < state->started || state->frame + 1 != frame ||
        state->x != x || state->y != y || state->width != width || state->height != height ||
        state->cursor != cursor || state->length != length || state->selection != selection || activity;
    if (reset) state->started = now;
    state->restart = false;
    state->frame = frame;
    state->x = x; state->y = y; state->width = width; state->height = height;
    state->cursor = cursor; state->length = length; state->selection = selection;
    // A full cycle is one second: visible for 500ms, hidden for 500ms.
    return fmod(now - state->started, 1.0) < 0.5;
}

#endif
