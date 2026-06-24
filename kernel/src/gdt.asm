global load_gdt

section .text

; rdi holds &gdtr
load_gdt:
    ; limine should have already disabled interrupts - just to be safe
    cli
    lgdt [rdi]

    ; force the CPU to use new code segment by doing a "far return"
    push 0x08                   ; Push the new Code Segment selector
    lea rax, [rel .reload_CS]   ; Get the address of the next instruction
    push rax                    ; Push it to the stack
    retfq                       ; Return Far (Pops the address and segment, forcing a reload)

.reload_CS:
    ; update the data segments
    ; data segment is at a 16 byte offset
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
