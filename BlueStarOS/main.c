// main.c (EDK II friendly, corrected)
// UEFI -> ExitBootServices -> Simple PIT-based preemptive multitasking POC
// WARNING: Experiment-only. Run in QEMU/OVMF.

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <stdint.h>

/* ----------------- Constants ----------------- */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_BASE_FREQ 1193182

#define IRQ0_VECTOR 0x20 /* after PIC remap */

/* ----------------- Task / Context ----------------- */
typedef struct
{
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} saved_regs_t;

#define STACK_SIZE_PAGES 4
#define STACK_SIZE_BYTES (STACK_SIZE_PAGES * 4096)

typedef struct
{
    uint8_t *stack_mem;
    uint64_t rsp; // pointer to saved_regs area (when task not running)
    int id;
} task_t;

#define MAX_TASKS 2
task_t tasks[MAX_TASKS];
int current_task = 0;
int task_count = 0;

UINT32 *fb;
UINTN width;
UINTN height;

/* VGA text buffer for simple output */
volatile uint16_t *VGA = (volatile uint16_t *)0xB8000;

/* ----------------- I/O helpers ----------------- */
static inline void out8(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" ::"a"(val), "Nd"(port));
}
static inline uint8_t in8(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void io_wait(void)
{
    __asm__ volatile("outb %%al, $0x80" ::"a"(0));
}

/* ----------------- PIC / PIT setup ----------------- */
static void pic_remap(int offset1, int offset2)
{
    uint8_t a1 = in8(PIC1_DATA);
    uint8_t a2 = in8(PIC2_DATA);

    out8(PIC1_CMD, 0x11);
    io_wait();
    out8(PIC2_CMD, 0x11);
    io_wait();

    out8(PIC1_DATA, offset1);
    io_wait();
    out8(PIC2_DATA, offset2);
    io_wait();

    out8(PIC1_DATA, 0x04);
    io_wait();
    out8(PIC2_DATA, 0x02);
    io_wait();

    out8(PIC1_DATA, 0x01);
    io_wait();
    out8(PIC2_DATA, 0x01);
    io_wait();

    out8(PIC1_DATA, a1);
    out8(PIC2_DATA, a2);
}

static void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
        out8(PIC2_CMD, PIC_EOI);
    out8(PIC1_CMD, PIC_EOI);
}

static void pit_set_frequency(uint32_t freq_hz)
{
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / freq_hz);
    out8(PIT_COMMAND_PORT, 0x36); // ch0, lobyte/hibyte, mode3
    io_wait();
    out8(PIT_CHANNEL0_PORT, divisor & 0xFF);
    io_wait();
    out8(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xFF);
}

static uint64_t gdt_table[3] = {
    0x0000000000000000ULL, // null
    0x00AF9A000000FFFFULL, // 64-bit code:  0x00 AF 9A 00 00 00 FF FF
    0x00CF92000000FFFFULL  // 64-bit data:  0x00 AF 92 00 00 00 FF FF
};

struct __attribute__((packed)) gdtr
{
    uint16_t limit;
    uint64_t base;
};

static void install_gdt(void)
{
    struct gdtr gdtr;
    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base = (uint64_t)&gdt_table;

    /* load GDTR */
    __asm__ volatile("lgdt %0" ::"m"(gdtr) : "memory");

    /* Far return (lretq) to reload CS, then reload data segment registers.
       This sequence is compatible with 64-bit long mode usage. */
    __asm__ volatile(
        "pushq $0x08\n\t" /* selector for code (entry 1) */
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t" /* reload CS with 0x08 and jump to label 1 */
        "1:\n\t"
        "mov $0x10, %%ax\n\t" /* selector for data (entry 2) */
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t" ::: "rax", "memory");
}

/* ----------------- IDT ----------------- */
typedef struct
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;
struct __attribute__((packed)) idtr_s
{
    uint16_t limit;
    uint64_t base;
};
#define IDT_ENTRIES 256
static idt_entry_t idt[IDT_ENTRIES];

static void set_idt_entry(int vec, void (*handler)(), uint8_t ist, uint8_t type_attr, uint16_t sel)
{
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low = addr & 0xFFFF;
    idt[vec].selector = sel;
    idt[vec].ist = ist & 0x7;
    idt[vec].type_attr = type_attr;
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero = 0;
}
static void load_idt(void)
{
    struct idtr_s idtr = {.limit = sizeof(idt) - 1, .base = (uint64_t)&idt};
    __asm__ volatile("lidt %0" ::"m"(idtr) : "memory");
}

uint8_t stack_memory[MAX_TASKS][STACK_SIZE_BYTES + 99];
int stack_memory_used = 0;

/* ----------------- Task initialization ----------------- */
static void task_init(task_t *t, void (*entry)(void), int id)
{

    t->stack_mem = stack_memory[stack_memory_used];
    stack_memory_used++;
    if (!t->stack_mem)
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    uint8_t *stack_top = t->stack_mem + STACK_SIZE_BYTES;
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

/* ----------------- Scheduler ----------------- */
/* non-static so ISR can call it */
__attribute__((used)) uint64_t EFIAPI scheduler(void *old_saved_area)
{
    task_t *cur = &tasks[current_task];
    cur->rsp = (uint64_t)old_saved_area;

    int next = (current_task + 1) % task_count;
    current_task = next;
    return tasks[next].rsp;
}

int cnt = 0;

/* ----------------- ISR stub for IRQ0 (timer) ----------------- */
/* naked function (no prologue/epilogue) */
__attribute__((naked)) void irq0_stub(void)
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

        "movq %%rsp, %%rcx\n\t" // arg0 = pointer to saved_regs area
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

__attribute__((naked)) void default_handler(void)
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

void vga_init()
{

    out8(0x3D4, 0x0A);
    out8(0x3D5, 0x00);
    out8(0x3D4, 0x0B);
    out8(0x3D5, 0x00);
}

/* ----------------- tasks ----------------- */
static void task_func_a(void)
{
    while (1)
    {
        for (int k = 0; k <= 256; k++)
        {
            for (int i = 1; i <= 100; i++)
            {
                for (int j = 1; j <= 100; j++)
                {
                    fb[i * width + j] = k * 0x100;
                }
            }
        }
    }
}

static void task_func_b(void)
{
    while (1)
    {
        for (int k = 0; k <= 256; k++)
        {
            for (int i = 101; i <= 200; i++)
            {
                for (int j = 101; j <= 200; j++)
                {
                    fb[i * width + j] = k;
                }
            }
        }
    }
}

/* ----------------- UEFI helpers ----------------- */
static EFI_STATUS exit_boot_services_and_prepare(EFI_HANDLE ImageHandle)
{
    EFI_STATUS Status;
    UINTN MapKey, DescriptorSize, MemMapSize = 0;
    UINT32 DescriptorVersion;
    EFI_MEMORY_DESCRIPTOR *MemMap = NULL;

    Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL)
    {
        Print(L"GetMemoryMap failed (expected BUFFER_TOO_SMALL): %r\n", Status);
        return Status;
    }

    MemMapSize += 2 * DescriptorSize;
    MemMap = AllocatePool(MemMapSize);
    if (!MemMap)
    {
        Print(L"AllocatePool failed\n");
        return EFI_OUT_OF_RESOURCES;
    }

    Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (EFI_ERROR(Status))
    {
        Print(L"GetMemoryMap failed: %r\n", Status);
        return Status;
    }

    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if (EFI_ERROR(Status))
    {
        Print(L"ExitBootServices failed: %r\n", Status);
        return Status;
    }
    return EFI_SUCCESS;
}

/* ----------------- main ----------------- */
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;

    // Do not call InitializeLib() in EDK2 modules that use UefiApplicationEntryPoint.
    // Print(L"Starting PIT multitasking demo. Run in QEMU/OVMF.\n");

    EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void **)&GOP);
    if (EFI_ERROR(Status))
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    GOP->SetMode(GOP, 0);

    fb = (UINT32 *)GOP->Mode->FrameBufferBase;
    width = GOP->Mode->Info->HorizontalResolution;
    height = GOP->Mode->Info->VerticalResolution;
    UINTN pitch = GOP->Mode->Info->PixelsPerScanLine;

    Status = exit_boot_services_and_prepare(ImageHandle);
    if (EFI_ERROR(Status))
    {
        Print(L"Failed to ExitBootServices: %r\n", Status);
        return Status;
    }

    install_gdt();

    load_idt();
    pic_remap(0x20, 0x28);

    uint8_t mask = in8(PIC1_DATA);
    mask &= ~(1 << 0);
    out8(PIC1_DATA, mask);

    task_count = 2;

    task_init(&tasks[0], task_func_a, 0);
    task_init(&tasks[1], task_func_b, 1);
    current_task = 0;

    for (int i = 0; i < IDT_ENTRIES; i++)
        set_idt_entry(i, default_handler, 0, 0x8E, KERNEL_CS);
    set_idt_entry(IRQ0_VECTOR, irq0_stub, 0, 0x8E, KERNEL_CS);

    pit_set_frequency(50);

    // Switch to first task by setting RSP to its saved area and iretq
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

    for (;;)
        __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
