[BITS 64]

mov rax, 0
lidt [rax]

dd 0xffffffff
dd 0xffffffff
