/* Shared evdev/Wayland <-> DOM button numbering. */
#ifndef LORD_MOUSEBUTTONS_H
#define LORD_MOUSEBUTTONS_H
#include <linux/input-event-codes.h>

static inline int mouse_button_from_evdev(unsigned code) {
    switch (code) {
    case BTN_LEFT: return 0;
    case BTN_MIDDLE: return 1;
    case BTN_RIGHT: return 2;
    case BTN_SIDE: return 3;
    case BTN_EXTRA: return 4;
    default: return -1;
    }
}

static inline int mouse_button_to_evdev(int button) {
    switch (button) {
    case 0: return BTN_LEFT;
    case 1: return BTN_MIDDLE;
    case 2: return BTN_RIGHT;
    case 3: return BTN_SIDE;
    case 4: return BTN_EXTRA;
    default: return -1;
    }
}
#endif
