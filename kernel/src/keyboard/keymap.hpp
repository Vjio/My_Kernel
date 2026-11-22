#pragma once
#include "keyboard.hpp"

struct keyboard_state {
    bool lshift, rshift;
    bool lctrl, rctrl;
    bool lalt, ralt;
    bool caps_lock;
    // true while the caps lock key itself is physically held
    bool caps_key_down;
};

void keyboard_state_update(struct keyboard_state &state, const key_event &event);

// US layout. translates a key press into a character, using the modifiers in state
// returns 0 if the key does not produce a character
// the result is printable ASCII, '\n', '\t', 0x7F (backspace), 0x1B (esc)
// or a control code (ctrl + letter gives 1..26)
char keycode_to_char(keycode_enum keycode, const struct keyboard_state &state);
