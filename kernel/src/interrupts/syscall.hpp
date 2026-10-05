#pragma once
#include <stdint.h>
#include <stddef.h>

#define O_RDONLY        0x00
#define O_WRONLY        0x01
#define O_RDWR          0x02
#define O_CREAT         0x40
#define O_TRUNC         0x200

// file offset is set to offset byte
#define SEEK_SET        0
// file offset is set to its current location plus offset bytes
#define SEEK_CUR        1
// file offset is set to the size of the file plus offset bytes
#define SEEK_END        2

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
uint64_t read(int fd, char *buf, size_t count);
// expands a process heap by length
// only call this from userland
struct brk_ret brk(size_t length);
// returns fd on success
// -1 on failure
int open(char *path, int flags);
// returns 0 on success
// -1 on failure
int close(int fd);
// returns fd on success
// -1 on failure
int dup(int oldfd);
int lseek(int fd, long offset, int whence);
int renameat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path);
int linkat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path, int flags);
// returns 0 on success, -1 on failure
int mkdir(char *path);
// flags is 0 for files, AT_REMOVEDIR for empty directories
// returns 0 on success, -1 on failure
int unlinkat(int dir_fd, char *path, int flags);
