global get_cr3
global load_cr3
global flush_tlb

section .text

get_cr3:
    mov rax, cr3
    ret

load_cr3:
    mov cr3, rdi
    ret

flush_tlb:
    invlpg [rdi]
    ret
