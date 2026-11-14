; userland syscall trampolines.
;
; int 0x80 does not clobber rcx/r11 the way the `syscall` instruction does,
; so arguments already sit exactly where the SysV C calling convention put
; them (rdi, rsi, rdx, ...) — these stubs only need to set the syscall
; number in rax and trap

global sys_read
global sys_write
global sys_open
global sys_close
global sys_lseek
global sys_brk
global sys_dup
global sys_sleep
global sys_clone
global sys_exit
global sys_mkdir
global sys_unlinkat
global sys_renameat
global sys_linkat
global sys_spawn

; long sys_read(int fd, void *buf, size_t count)
; rdi -> fd, rsi -> buf, rdx -> count
sys_read:
    mov rax, 0
    int 0x80
    ret

; long sys_write(int fd, const void *buf, size_t count)
; rdi -> fd, rsi -> buf, rdx -> count
sys_write:
    mov rax, 1
    int 0x80
    ret

; int sys_open(char *path, int flags)
; rdi -> path, rsi -> flags
; returns the fd, -1 on failure
sys_open:
    mov rax, 2
    int 0x80
    ret

; int sys_close(int fd)
; rdi -> fd
; returns 0 on success, -1 on failure
sys_close:
    mov rax, 3
    int 0x80
    ret

; int sys_lseek(int fd, long offset, int whence)
; rdi -> fd, rsi -> offset, rdx -> whence
sys_lseek:
    mov rax, 8
    int 0x80
    ret

; struct brk_ret sys_brk(size_t length)
; rdi -> length
; syscall_handler writes the address into frame->rax and the length into
; frame->rdx
sys_brk:
    mov rax, 12
    int 0x80
    ret

; int sys_dup(int oldfd)
; rdi -> oldfd
; returns the new fd, -1 on failure
sys_dup:
    mov rax, 32
    int 0x80
    ret

; void sys_sleep(uint64_t ticks)
; rdi -> ticks
sys_sleep:
    mov rax, 55
    int 0x80
    ret

; uint64_t sys_clone(char *name, void (*function)(void *), void *arg)
; rdi -> name, rsi -> function, rdx -> arg
; makes a new thread in the current process. returns 0 on failure
sys_clone:
    mov rax, 56
    int 0x80
    ret

; void sys_exit(void)
sys_exit:
    mov rax, 60
    int 0x80
    ret

; int sys_mkdir(char *path)
; rdi -> path
; returns 0 on success, -1 on failure
sys_mkdir:
    mov rax, 83
    int 0x80
    ret

; int sys_unlinkat(int dir_fd, char *path, int flags)
; rdi -> dir_fd, rsi -> path, rdx -> flags
; flags is 0 for files, AT_REMOVEDIR for empty directories
; returns 0 on success, -1 on failure
sys_unlinkat:
    mov rax, 263
    int 0x80
    ret

; int sys_renameat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path)
; rdi -> old_dir_fd, rsi -> old_path, rdx -> new_dir_fd, rcx -> new_path
sys_renameat:
    mov rax, 264
    int 0x80
    ret

; int sys_linkat(int old_dir_fd, char *old_path, int new_dir_fd, char *new_path, int flags)
; rdi -> old_dir_fd, rsi -> old_path, rdx -> new_dir_fd, rcx -> new_path, r8 -> flags
sys_linkat:
    mov rax, 265
    int 0x80
    ret

; long sys_spawn(char *path, char *argv[])
; rdi -> path, rsi -> argv (NULL terminated array of NUL terminated strings)
; returns the new pid in rax, -1 on failure
sys_spawn:
    mov rax, 500
    int 0x80
    ret
