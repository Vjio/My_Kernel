#pragma once
#include <stdint.h>
#include <stddef.h>

#define DIRENT_NAME_MAX         64
#define GETDENTS_MAX_ENTRIES    64

struct brk_ret {
    void *address;
    uint64_t length;
};

struct dirent {
    uint32_t inode_nr;
    char name[DIRENT_NAME_MAX];
};

extern "C" {
    long sys_read(int fd, void *buf, size_t count);
    long sys_write(int fd, const void *buf, size_t count);
    // returns the fd on success, -1 on failure
    int sys_open(char *path, int flags);
    // returns 0 on success, -1 on failure
    int sys_close(int fd);
    int sys_lseek(int fd, long offset, int whence);
    struct brk_ret sys_brk(size_t length);
    // returns the new fd on success, -1 on failure
    int sys_dup(int oldfd);
    void sys_sleep(uint64_t ticks);
    // makes a new thread in the current process. returns 0 on failure
    uint64_t sys_clone(char *name, void (*function)(void *), void *arg);
    void sys_exit(void);
    // returns 0 on success, -1 on failure
    int sys_mkdir(char *path);
    // flags is 0 for files, AT_REMOVEDIR for empty directories
    // returns 0 on success, -1 on failure
    int sys_unlinkat(int dir_fd, char *path, int flags);
    int sys_renameat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path);
    int sys_linkat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path, int flags);
    // returns the new pid on success, -1 on failure
    long sys_spawn(char *path, char *argv[]);
    // reads up to max_entries (at most GETDENTS_MAX_ENTRIES) entries of the directory
    // returns the number of entries written, 0 at the end of the directory, -1 on failure
    int sys_getdents(int fd, struct dirent *buf, size_t max_entries);
}
