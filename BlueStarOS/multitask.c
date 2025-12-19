#include "multitask.h"
#include "gdt.h"
#include "heap.h"

#define MAX_TASKS 3
#define TASK_STACK_SIZE 4 * 4096


task_t tasks[MAX_TASKS];
int current_task = 0;
int task_count = 0;


/* ----------------- Task initialization ----------------- */
void task_add(void (*entry)(void), int id)
{
    task_t *t = &tasks[task_count];

    t->stack_mem = malloc(TASK_STACK_SIZE + 99);
    task_count++;
    if (!t->stack_mem)
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    uint8_t *stack_top = t->stack_mem + TASK_STACK_SIZE;
    uint64_t regs_size = sizeof(saved_regs_t);
    uint64_t iret_size = sizeof(uint64_t) * 5; // RIP, CS, RFLAGS
    uint8_t *saved_area = stack_top - regs_size - iret_size;

    uint64_t *iret_frame = (uint64_t *)(saved_area + regs_size);
    iret_frame[0] = (uint64_t)entry;
    iret_frame[1] = (uint64_t)KERNEL_CS;
    iret_frame[2] = (uint64_t)0x202;
    iret_frame[3] = (uint64_t)stack_top;
    iret_frame[4] = 0x10;

    saved_regs_t *regs = (saved_regs_t *)saved_area;
    regs->rax = 0;
    regs->rcx = 0;
    regs->rdx = 0;
    regs->rbx = 0;
    regs->rbp = 0;
    regs->rsi = 0;
    regs->rdi = (uint64_t)id;
    regs->r8 = 0;
    regs->r9 = 0;
    regs->r10 = 0;
    regs->r11 = 0;
    regs->r12 = 0;
    regs->r13 = 0;
    regs->r14 = 0;
    regs->r15 = 0;

    t->rsp = (uint64_t)saved_area;
    t->id = id;
}

__attribute__((used)) uint64_t scheduler(void *old_saved_area)
{
    task_t *cur = tasks + current_task;
    cur->rsp = (uint64_t)old_saved_area;

    int next = (current_task + 1) % task_count;
    current_task = next;
    return tasks[next].rsp;
}

__attribute__((naked)) void irq0_task_switch(void)
{

    __asm__ volatile(
        "pushq %%rax\n\t"
        "pushq %%rcx\n\t"
        "pushq %%rdx\n\t"
        "pushq %%rbx\n\t"
        "pushq %%rbp\n\t"
        "pushq %%rsi\n\t"
        "pushq %%rdi\n\t"
        "pushq %%r8\n\t"
        "pushq %%r9\n\t"
        "pushq %%r10\n\t"
        "pushq %%r11\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"

        "movq %%rsp, %%rdi\n\t" // arg0 = pointer to saved_regs area
        "call scheduler\n\t"
        "movq %%rax, %%rsp\n\t" // switch to next task's saved_regs area
        ::: "memory");

    __asm__ volatile(
        /* send EOI to PIC */
        "movb $0x20, %%al\n\t"
        "outb %%al, $0x20\n\t"

        /* pop registers from new stack (scheduler set rsp) */
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%r11\n\t"
        "popq %%r10\n\t"
        "popq %%r9\n\t"
        "popq %%r8\n\t"
        "popq %%rdi\n\t"
        "popq %%rsi\n\t"
        "popq %%rbp\n\t"
        "popq %%rbx\n\t"
        "popq %%rdx\n\t"
        "popq %%rcx\n\t"
        "popq %%rax\n\t"

        "iretq\n\t" ::: "memory");
}

void multitask_start(){
    __asm__ volatile(
        "mov %0, %%rsp\n\t"
        "popq %%rax\n\t"
        "popq %%rcx\n\t"
        "popq %%rdx\n\t"
        "popq %%rbx\n\t"
        "popq %%rbp\n\t"
        "popq %%rsi\n\t"
        "popq %%rdi\n\t"
        "popq %%r8\n\t"
        "popq %%r9\n\t"
        "popq %%r10\n\t"
        "popq %%r11\n\t"
        "popq %%r12\n\t"
        "popq %%r13\n\t"
        "popq %%r14\n\t"
        "popq %%r15\n\t"
        "iretq\n\t" ::"r"(tasks[0].rsp) : "memory");

    for(;;) asm("hlt");

}