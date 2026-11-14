#pragma once
#include <cstdint>
#include <cstddef>
#include "keyboard.hpp"

#define EXT_KEY_PREFIX_SCAN_CODE_1  0x0E
#define RELEASE_FLAG_MASK           0x80
#define SCANCODE_MASK               0x7F

// this array is indexed by the scan codes of base 1
// example: scan code 0x03 is at index 3 in the array and hold the keycode for that key
// in order to add support for scan code 2 & 3, make a new scan_code array like this onde
static const keycode_enum scan_code1_to_keycode[0x80] = {
    /* 0x00 */ KEY_NONE, KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6,
               KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB,
    /* 0x10 */ KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I,
               KEY_O, KEY_P, KEY_LBRACKET, KEY_RBRACKET, KEY_ENTER, KEY_LCTRL, KEY_A, KEY_S,
    /* 0x20 */ KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON,
               KEY_APOSTROPHE, KEY_GRAVE, KEY_LSHIFT, KEY_BACKSLASH, KEY_Z, KEY_X, KEY_C, KEY_V,
    /* 0x30 */ KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_PERIOD, KEY_SLASH, KEY_RSHIFT, KEY_KP_MUL,
               KEY_LALT, KEY_SPACE, KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    /* 0x40 */ KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_NUMLOCK, KEY_SCROLLLOCK, KEY_KP_7,
               KEY_KP_8, KEY_KP_9, KEY_KP_MINUS, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_PLUS, KEY_KP_1,
    /* 0x50 */ KEY_KP_2, KEY_KP_3, KEY_KP_0, KEY_KP_DOT, KEY_NONE, KEY_NONE, KEY_NONE, KEY_F11,
               KEY_F12,
};

// 0xE0-prefixed scancodes
struct ext_entry{
    uint8_t scancode;
    keycode_enum keycode;
};

// same as scan_code1 but for the extra scancodes, in extended format
static const ext_entry set_code1_ext[] = {
    {0x1C, KEY_KP_ENTER}, {0x1D, KEY_RCTRL},   {0x35, KEY_KP_DIV},
    {0x37, KEY_PRINTSCREEN}, {0x38, KEY_RALT},
    {0x47, KEY_HOME},  {0x48, KEY_UP},    {0x49, KEY_PAGEUP},
    {0x4B, KEY_LEFT},  {0x4D, KEY_RIGHT},
    {0x4F, KEY_END},   {0x50, KEY_DOWN},  {0x51, KEY_PAGEDOWN},
    {0x52, KEY_INSERT},{0x53, KEY_DELETE},
    {0x5B, KEY_LGUI},  {0x5C, KEY_RGUI},  {0x5D, KEY_MENU},
};

void handle_scancode(uint8_t byte);

// convers a set1 scancode to keycode
// returns false if byte was an indication that the next event is an extended scancode
// (in this case, key_event will be unchanged. you will need to way for the next event
// to get the keycode)
bool set1_to_keycode(uint8_t byte, struct key_event& key_event);