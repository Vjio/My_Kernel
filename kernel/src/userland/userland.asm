; userland syscall trampolines.
;
; int 0x80 does not clobber rcx/r11 the way the `syscall` instruction does,
; so arguments already sit exactly where the SysV C calling convention put
; them (rdi, rsi, rdx, ...) — these stubs only need to set the syscall
; number in rax and trap. no argument shuffling required.

global sys_write
global sys_sleep
global sys_clone
global sys_sleep
global sys_brk


; long sys_write(int fd, const void *buf, size_t count)
; rdi -> fd, rsi -> buf, rdx -> count
sys_write:
    mov rax, 1
    int 0x80
    ret

; void sleep(uint8_t ticks)
; rdi -> ticks
sys_sleep
    mov rax, 15
    int 0x80
    ret

sys_exit
    mov rax, 60
    int 0x80
    ret

; uint64_t clone(bool new_process_flag, char *name, uint64_t entry_point, void *arg) syscall,
; rdi -> new_process_flag,  rsi -> name
; rdx -> entry_point,       r8 -> arg
sys_clone
    mov rax, 56
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
