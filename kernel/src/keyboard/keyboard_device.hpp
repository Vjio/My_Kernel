#pragma once
#include <cstdint>
#include "keyboard.hpp"

// this represents the api that TTY uses to get its data

class KeyboardDevice {
    private:

    public:
    // blocks until a keyboard event is available
    virtual void keyboard_get_event(key_event& out) = 0;
    // non-blocking
    // returns false if no keyboard event is available
    virtual bool keyboard_try_get_event(key_event& out) = 0;

    virtual ~KeyboardDevice() = default;
};
