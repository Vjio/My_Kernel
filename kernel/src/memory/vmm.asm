global get_cr3
global flush_tlb

section .text

get_cr3:
    mov rax, cr3
    ret

flush_tlb:
    invlpg [rdi]
    ret
