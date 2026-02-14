#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

/* Physical addresses shared between UEFI loader and kernel */
#define KERNEL_LOAD_ADDR 0x200000ULL
#define BOOT_INFO_ADDR   0x180000ULL

/* framebuffer_format values */
#define FB_FORMAT_UNKNOWN 0U
#define FB_FORMAT_BGRX    1U
#define FB_FORMAT_RGBX    2U

typedef struct {
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint32_t framebuffer_format;
    uint64_t heap_base;
    uint64_t heap_size;
    uint64_t fat_partition_lba;
} boot_info_t;

#endif
