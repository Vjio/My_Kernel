#include "keyboard_scancode_handler.hpp"
#include "keyboard_driver.hpp"

extern KeyboardDriver *g_keyboard_driver;

void handle_scancode(uint8_t byte) {
    struct key_event key_event;
    if (set1_to_keycode(byte, key_event) && g_keyboard_driver != nullptr)
        ring_buffer_enqueue(g_keyboard_driver->rb, &key_event, 1);
}

bool set1_to_keycode(uint8_t byte, struct key_event& key_event) {
    static bool extended = false;

    if (byte == 0xE0) {
        extended = true;
        return false;
    }

    bool released = byte & RELEASE_FLAG_MASK;
    uint8_t scancode = byte & SCANCODE_MASK;
    keycode_enum keycode = KEY_NONE;

    if (extended) {
        extended = false;
        for (const auto &entry : set_code1_ext)
            if (entry.scancode == scancode) {
                keycode = entry.keycode; 
                break;
            }
    } else {
        keycode = scan_code1_to_keycode[scancode];
    }

    if (keycode == KEY_NONE) return false;
    
    key_event.keycode = keycode;
    key_event.released = released;

    return true;
}
