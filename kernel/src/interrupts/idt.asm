global load_idt
extern exception_handler
extern irq_handler
extern syscall_handler

; macro definitions
; some interrupts automatically push an error code
; so we define these macros as to keep the stack alligned
%macro isr_err_stub 1
isr_stub_%+%1:
    push qword %1
    jmp exception_stub
%endmacro

%macro isr_no_err_stub 1
isr_stub_%+%1:
    ; push dummy error code
    push qword 0
    push qword %1
    jmp exception_stub
%endmacro

isr_no_err_stub 0
isr_no_err_stub 1
isr_no_err_stub 2
isr_no_err_stub 3
isr_no_err_stub 4
isr_no_err_stub 5
isr_no_err_stub 6
isr_no_err_stub 7
isr_err_stub    8
isr_no_err_stub 9
isr_err_stub    10
isr_err_stub    11
isr_err_stub    12
isr_err_stub    13
isr_err_stub    14
isr_no_err_stub 15
isr_no_err_stub 16
isr_err_stub    17
isr_no_err_stub 18
isr_no_err_stub 19
isr_no_err_stub 20
isr_no_err_stub 21
isr_no_err_stub 22
isr_no_err_stub 23
isr_no_err_stub 24
isr_no_err_stub 25
isr_no_err_stub 26
isr_no_err_stub 27
isr_no_err_stub 28
isr_no_err_stub 29
isr_err_stub    30
isr_no_err_stub 31

; irq's dont push error codes so we push a dummy
; along with incrementing the vector/int_no by 32 (because we remapped the PIC)
%macro irq_stub 1
irq_stub_%+%1:
    push qword 0            ; Push dummy error code
    push qword (%1 + 32)    ; Push vector number (IRQ + 32)
    jmp irq_common_stub
%endmacro

irq_stub 0
irq_stub 1
irq_stub 2
irq_stub 3
irq_stub 4
irq_stub 5
irq_stub 6
irq_stub 7
irq_stub 8
irq_stub 9
irq_stub 10
irq_stub 11
irq_stub 12
irq_stub 13
irq_stub 14
irq_stub 15

; Stack at entry:
;   [RSP+0]   index number of interrupt
;   [RSP+8]   error_code
;   [RSP+16]  rip
;   [RSP+24]  cs
;   [RSP+32]  rflags
;   [RSP+40]  rsp
;   [RSP+48]  ss

exception_stub:
    ; push other registers here, if you want
    ; (first modify the frame struct to contains these new registers!)

    ; rsp now points at the base of a manually constructed
    ; interrupt_frame struct, on the stack
    mov rdi, rsp
    call exception_handler

    ; restore any additionally pushed registers

    ; clean up vector + error_code that we (or the CPU) pushed
    add rsp, 16

    iretq

irq_common_stub:
    ; save all registers because we intend to
    ; go back to program that was running when the interrupt apepared
    ; without clobbering its registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; pass every register to function as a struct on the stack
    lea rdi, [rsp]

    call irq_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; clean dummy err code and interrupt number
    add rsp, 16

    ; return to whatever code was running before the interrupt fired
    iretq
syscall_stub:
    push qword 0    ; dummy error code
    push qword 0x80 ; vector number
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; pass every register to function as a struct on the stack
    lea rdi, [rsp]
    call syscall_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; return to whatever code was running before the interrupt fired
    add rsp, 16
    iretq

load_idt:
    lidt [rdi]
    ; activate interrupts
    ; sti 
    ret

global isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
global isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
global isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
global isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
global isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
global isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
global isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
global isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31

global irq_stub_0,  irq_stub_1,  irq_stub_2,  irq_stub_3
global irq_stub_4,  irq_stub_5,  irq_stub_6,  irq_stub_7
global irq_stub_8,  irq_stub_9,  irq_stub_10, irq_stub_11
global irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15

global syscall_stub
