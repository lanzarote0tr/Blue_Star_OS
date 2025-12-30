#include "multitask.h"
#include "gdt.h"
#include "heap.h"
#include "asmtools.h"
#include "gui.h"

#define MAX_TASKS 20
#define TASK_STACK_SIZE 4096


task_t tasks[MAX_TASKS];

int current_task = 0;
int task_count = 0;


/* ----------------- Task initialization ----------------- */
void task_add(void (*entry)(void), int id, int cs, int ds)
{
    task_t *t = &tasks[task_count];

    t->stack_mem = malloc(TASK_STACK_SIZE);

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
    iret_frame[1] = (uint64_t)cs;
    iret_frame[2] = (uint64_t)0x202;
    iret_frame[3] = (uint64_t)iret_frame;
    iret_frame[4] = ds;

    saved_regs_t *regs = (saved_regs_t *)saved_area;
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

    PUSHAQ();

    __asm__ volatile(

        "movq %%rsp, %%rdi\n\t" // arg0 = pointer to saved_regs area
        "call scheduler\n\t"
        "movq %%rax, %%rsp\n\t" // switch to next task's saved_regs area
        ::: "memory");

    SEND_EOI();
    POPAQ();
    IRETQ();
}

void multitask_start(){

    __asm__ volatile(
        "mov %0, %%rsp\n\t" ::"r"(tasks[0].rsp) : "memory"
    );

    POPAQ();
    IRETQ();

    for(;;) asm("hlt");

}