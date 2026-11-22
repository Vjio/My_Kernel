#include "keymap.hpp"

struct key_chars {
    keycode_enum keycode;
    char normal;
    char shifted;
};

static const struct key_chars key_table[] = {
    {KEY_1, '1', '!'}, {KEY_2, '2', '@'}, {KEY_3, '3', '#'}, {KEY_4, '4', '$'},
    {KEY_5, '5', '%'}, {KEY_6, '6', '^'}, {KEY_7, '7', '&'}, {KEY_8, '8', '*'},
    {KEY_9, '9', '('}, {KEY_0, '0', ')'}, {KEY_MINUS, '-', '_'}, {KEY_EQUALS, '=', '+'},

    {KEY_Q, 'q', 'Q'}, {KEY_W, 'w', 'W'}, {KEY_E, 'e', 'E'}, {KEY_R, 'r', 'R'},
    {KEY_T, 't', 'T'}, {KEY_Y, 'y', 'Y'}, {KEY_U, 'u', 'U'}, {KEY_I, 'i', 'I'},
    {KEY_O, 'o', 'O'}, {KEY_P, 'p', 'P'}, {KEY_LBRACKET, '[', '{'}, {KEY_RBRACKET, ']', '}'},

    {KEY_A, 'a', 'A'}, {KEY_S, 's', 'S'}, {KEY_D, 'd', 'D'}, {KEY_F, 'f', 'F'},
    {KEY_G, 'g', 'G'}, {KEY_H, 'h', 'H'}, {KEY_J, 'j', 'J'}, {KEY_K, 'k', 'K'},
    {KEY_L, 'l', 'L'}, {KEY_SEMICOLON, ';', ':'}, {KEY_APOSTROPHE, '\'', '"'},
    {KEY_GRAVE, '`', '~'}, {KEY_BACKSLASH, '\\', '|'},

    {KEY_Z, 'z', 'Z'}, {KEY_X, 'x', 'X'}, {KEY_C, 'c', 'C'}, {KEY_V, 'v', 'V'},
    {KEY_B, 'b', 'B'}, {KEY_N, 'n', 'N'}, {KEY_M, 'm', 'M'}, {KEY_COMMA, ',', '<'},
    {KEY_PERIOD, '.', '>'}, {KEY_SLASH, '/', '?'}, {KEY_SPACE, ' ', ' '},

    // keypad keys that don't depend on num lock
    {KEY_KP_DIV, '/', '/'}, {KEY_KP_MUL, '*', '*'}, {KEY_KP_MINUS, '-', '-'},
    {KEY_KP_PLUS, '+', '+'},
};

void keyboard_state_update(struct keyboard_state &state, const key_event &event) {
    bool down = !event.released;

    switch (event.keycode) {
        case KEY_LSHIFT:    state.lshift = down; break;
        case KEY_RSHIFT:    state.rshift = down; break;
        case KEY_LCTRL:     state.lctrl = down; break;
        case KEY_RCTRL:     state.rctrl = down; break;
        case KEY_LALT:      state.lalt = down; break;
        case KEY_RALT:      state.ralt = down; break;

        case KEY_CAPSLOCK:
            // toggle only when the key goes from up to down. a held key sends its
            // press event over and over which must not toggle again
            if (down && !state.caps_key_down)
                state.caps_lock = !state.caps_lock;
            state.caps_key_down = down;
            break;

        default:
            break;
    }
}

char keycode_to_char(keycode_enum keycode, const struct keyboard_state &state) {
    bool shift = state.lshift || state.rshift;
    bool ctrl = state.lctrl || state.rctrl;

    switch (keycode) {
        case KEY_ENTER:
        case KEY_KP_ENTER:  return '\n';
        case KEY_TAB:       return '\t';
        case KEY_BACKSPACE: return 0x7F;
        case KEY_ESC:       return 0x1B;
        default:            break;
    }

    for (const struct key_chars &k : key_table) {
        if (k.keycode != keycode)
            continue;

        bool letter = k.normal >= 'a' && k.normal <= 'z';
        bool upper = letter ? (shift != state.caps_lock) : shift;
        char c = upper ? k.shifted : k.normal;

        // ctrl + a = 1, ctrl + c = 3, ctrl + d = 4, ... ctrl + z = 26
        if (ctrl && letter)
            return c & 0x1F;
        return c;
    }

    return 0;
}
