#pragma once
#include <stdint.h>
#include <stddef.h>

struct brk_ret {
    void *address;
    uint64_t length;
};

// makes a new thread
// new_process_flag will be set when the caller wants to make a whole new process
// if flag not set, just adds another thread to current process
uint64_t clone(bool new_process_flag, char *name, uint64_t entry_point, void *arg);
void exit();
void sleep(uint64_t ticks);
// fd will be ignored for now since system can only write to console
uint64_t write(int fd, char *buf, size_t count);
// expands a process heap by length
// only call this from userland
struct brk_ret brk(size_t length);
