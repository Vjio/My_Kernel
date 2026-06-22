global load_tss

load_tss:
    ; 0x18 is the byte offset of Entry 3 in the GDT
    mov ax, 0x18 
    ltr ax
    ret
