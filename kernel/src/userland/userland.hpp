#pragma once
#include <stdint.h>
#include <stddef.h>

struct brk_ret {
    void *address;
    uint64_t length;
};

extern "C" {
    size_t sys_write(int fd, const void *buf, size_t count);
    struct brk_ret sys_brk(size_t length);
    uint64_t clone(bool new_process_flag, char *name, uint64_t entry_point, void *arg);
    void sleep(uint64_t ticks);
    void exit();
}
