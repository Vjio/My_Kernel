#pragma once
#include "keyboard_device.hpp"
#include "keyboard.hpp"
#include "ring_buffer.hpp"

class KeyboardDriver : public KeyboardDevice {
    public:
    struct ring_buffer *rb = nullptr;

    KeyboardDriver() {
        rb = ring_buffer_init(RB_DEFAULT_CAP);
    }

    void keyboard_get_event(key_event& out) override;
    bool keyboard_try_get_event(key_event& out) override;
};
