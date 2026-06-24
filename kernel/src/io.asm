global inb
global outb
global io_wait
global cpu_get_msr
global cpu_set_msr


section .text

; uint8_t inb(uint16_t port)
; port is passed in DI
inb:
    mov dx, di      ; source has to be dx or imm value
    in al, dx       ; read from port DX into AL
    ret

; void outb(uint16_t port, uint8_t val)
; port is passed in DI, val is passed in SI
outb:
    mov dx, di      ; move port into DX
    mov ax, si      ; move val into AX. source has to be ax
    out dx, al      ; write AL to port DX. destination has to be dx
    ret

; void io_wait(void)
io_wait:
    mov dx, 0x80    ; port 0x80 (POST checkpoint)
    mov al, 0       ; dummy byte
    out dx, al      ; write to port to burn a few microseconds
    ret

; uint64_t cpu_get_msr(uint32_t msr)
; msr passed in EDI (64-bit ABI)
cpu_get_msr:
    mov ecx, edi
    rdmsr           ; Reads MSR specified by ECX into EDX:EAX
    shl rdx, 32     ; Shift high bits into position
    or rax, rdx     ; Combine EAX and EDX into RAX
    ret             ; Return full 64-bit value in RAX

; void cpu_set_msr(uint32_t msr, uint64_t value)
; msr in EDI, value in RSI
cpu_set_msr:
    mov ecx, edi
    mov eax, esi    ; Low 32 bits of value into EAX
    shr rsi, 32
    mov edx, esi    ; High 32 bits of value into EDX
    wrmsr           ; Write EDX:EAX to MSR specified by ECX
    ret
