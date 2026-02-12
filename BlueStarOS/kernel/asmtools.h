#ifndef ASMTOOLS_H
#define ASMTOOLS_H

/* Save/restore common GPRs in the order expected by saved_regs_t */
#define PUSHAQ() \
    __asm__ volatile( \
        "pushq %%rax\n\t" \
        "pushq %%rcx\n\t" \
        "pushq %%rdx\n\t" \
        "pushq %%rbx\n\t" \
        "pushq %%rbp\n\t" \
        "pushq %%rsi\n\t" \
        "pushq %%rdi\n\t" \
        "pushq %%r8\n\t" \
        "pushq %%r9\n\t" \
        "pushq %%r10\n\t" \
        "pushq %%r11\n\t" \
        "pushq %%r12\n\t" \
        "pushq %%r13\n\t" \
        "pushq %%r14\n\t" \
        "pushq %%r15\n\t" \
        ::: "memory" \
    )

#define POPAQ() \
    __asm__ volatile( \
        "popq %%r15\n\t" \
        "popq %%r14\n\t" \
        "popq %%r13\n\t" \
        "popq %%r12\n\t" \
        "popq %%r11\n\t" \
        "popq %%r10\n\t" \
        "popq %%r9\n\t" \
        "popq %%r8\n\t" \
        "popq %%rdi\n\t" \
        "popq %%rsi\n\t" \
        "popq %%rbp\n\t" \
        "popq %%rbx\n\t" \
        "popq %%rdx\n\t" \
        "popq %%rcx\n\t" \
        "popq %%rax\n\t" \
        ::: "memory" \
    )

#define IRETQ() __asm__ volatile("iretq\n\t" ::: "memory")

/* Send EOI to PIC master (IRQ0 path in this kernel) */
#define SEND_EOI() \
    __asm__ volatile( \
        "movb $0x20, %%al\n\t" \
        "outb %%al, $0x20\n\t" \
        ::: "memory" \
    )

#endif
