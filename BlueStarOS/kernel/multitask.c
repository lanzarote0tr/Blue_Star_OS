#include "multitask.h"
#include "gdt.h"
#include "heap.h"
#include "asmtools.h"

#define MAX_TASKS 20U
#define TASK_STACK_SIZE 4096U

static task_t tasks[MAX_TASKS];
static int current_task = 0;
static int task_count = 0;

static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void clear_saved_regs(saved_regs_t *regs, int id, uint8_t *saved_area)
{
    regs->rax = 0;
    regs->rcx = 0;
    regs->rdx = 0;
    regs->rbx = 0;
    regs->rbp = (uint64_t)saved_area;
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
}

void task_add(void (*entry)(void), int id, int cs, int ds)
{
    if (task_count >= (int)MAX_TASKS) {
        halt_forever();
    }

    task_t *t = &tasks[task_count];
    t->user_stack_mem = malloc(TASK_STACK_SIZE);
    t->kernel_stack_mem = malloc(TASK_STACK_SIZE);
    if (t->user_stack_mem == NULL || t->kernel_stack_mem == NULL) {
        halt_forever();
    }

    uint64_t user_stack_top = ((uint64_t)(t->user_stack_mem + TASK_STACK_SIZE)) & ~0xFULL;
    uint64_t kernel_stack_top = ((uint64_t)(t->kernel_stack_mem + TASK_STACK_SIZE)) & ~0xFULL;
    uint64_t regs_size = sizeof(saved_regs_t);
    int user_mode = (cs & 0x3) == 0x3;
    uint64_t iret_size = sizeof(uint64_t) * (user_mode ? 5 : 3);
    uint8_t *saved_area = (uint8_t *)(kernel_stack_top - regs_size - iret_size);

    uint64_t *iret_frame = (uint64_t *)(saved_area + regs_size);
    iret_frame[0] = (uint64_t)entry;
    iret_frame[1] = (uint64_t)cs;
    iret_frame[2] = (uint64_t)0x202;
    if (user_mode)
    {
        iret_frame[3] = user_stack_top;
        iret_frame[4] = (uint64_t)ds;
    }

    saved_regs_t *regs = (saved_regs_t *)saved_area;
    clear_saved_regs(regs, id, saved_area);

    t->rsp = (uint64_t)saved_area;
    t->kernel_stack_top = kernel_stack_top;
    t->id = id;

    task_count++;
}

__attribute__((used)) uint64_t scheduler(void *old_saved_area)
{
    if (task_count == 0) {
        return (uint64_t)old_saved_area;
    }

    task_t *cur = tasks + current_task;
    cur->rsp = (uint64_t)old_saved_area;

    int next = (current_task + 1) % task_count;
    current_task = next;
    tss_set_rsp0(tasks[next].kernel_stack_top);
    return tasks[next].rsp;
}

__attribute__((naked)) void irq0_task_switch(void)
{
    PUSHAQ();
    __asm__ volatile(
        "movq %%rsp, %%rdi\n\t"
        "call scheduler\n\t"
        "movq %%rax, %%rsp\n\t"
        ::: "memory");

    SEND_EOI();
    POPAQ();
    IRETQ();
}

void multitask_start(void)
{
    if (task_count == 0) {
        halt_forever();
    }

    tss_set_rsp0(tasks[0].kernel_stack_top);

    __asm__ volatile(
        "mov %0, %%rsp\n\t" ::"r"(tasks[0].rsp) : "memory"
    );

    POPAQ();
    IRETQ();

    halt_forever();
}
