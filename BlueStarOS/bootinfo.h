#ifndef BOOTINFO_H
#define BOOTINFO_H

#define KERNEL_LOAD_ADDR 0x200000
#define BOOT_INFO_ADDR 0x180000

#include <stdint.h>

typedef struct {
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
} boot_info_t;

#endif