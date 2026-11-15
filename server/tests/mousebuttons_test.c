#include "mousebuttons.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const unsigned codes[] = { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, BTN_SIDE, BTN_EXTRA };
    for (int i = 0; i < 5; i++) {
        assert(mouse_button_from_evdev(codes[i]) == i);
        assert(mouse_button_to_evdev(i) == (int)codes[i]);
        /* Press and release use the same code through compositor and app client. */
        assert(mouse_button_from_evdev(mouse_button_to_evdev(i)) == i);
    }
    assert(mouse_button_from_evdev(KEY_BACK) == -1);
    assert(mouse_button_from_evdev(BTN_TOUCH) == -1);
    assert(mouse_button_from_evdev(BTN_FORWARD) == -1);
    assert(mouse_button_to_evdev(-1) == -1);
    assert(mouse_button_to_evdev(5) == -1);
    assert(mouse_button_to_evdev(256) == -1);
    puts("mousebuttons: ok");
    return 0;
}
