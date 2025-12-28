#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>
#define BOOT_INFO_ADDRESS 0x180000
#define KERNEL_LOAD_ADDRESS 0x200000

typedef struct {
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
} boot_info_t;

#endif