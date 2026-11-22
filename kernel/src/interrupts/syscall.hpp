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

#define DIRENT_NAME_MAX         64
// most entries a single getdents call will return
#define GETDENTS_MAX_ENTRIES    64

// mirror of entry struct defined in vsfs_layout
struct dirent {
    uint32_t inode_nr;
    char name[DIRENT_NAME_MAX];
};

struct brk_ret {
    void *address;
    uint64_t length;
};

// makes a new thread for the current process
// returns 0 on failure
uint64_t clone(char *name, void(*function)(void*), void *arg);
// starts the ELF at path as a new process, named after the file
// argv is a NULL terminated array of NUL terminated strings (NULL is the same as an empty argv).
// at most MAX_ARGC strings and MAX_ARG_BYTES bytes in total
// the child's main gets argc in rdi and argv in rsi
// returns the new pid on success, -1 on failure
long spawn(char *path, char *argv[]);

void exit();
void sleep(uint64_t ticks);

// expands a process heap by length
// only call this from userland
struct brk_ret brk(size_t length);

// fd will be ignored for now since system can only write to console
// -1 on failure
long write(int fd, char *buf, size_t count);
// -1 on failure
long read(int fd, char *buf, size_t count);
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

// reads up to max_entries entries of the directory open at fd into buf
// at most GETDENTS_MAX_ENTRIES are returned per call, "." and ".." are included
// returns the number of entries written, 0 at the end of the directory, -1 on failure
int getdents(int fd, struct dirent *buf, size_t max_entries);
