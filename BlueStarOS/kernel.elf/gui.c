#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <stdint.h>
#include "bootinfo.h"

uint32_t *fb;
uint32_t double_fb[5000000];
uint64_t width;
uint64_t height;

void init_gui(boot_info_t* bootinfo)
{

    fb = bootinfo->framebuffer_base;
    width = bootinfo->framebuffer_width;
    height = bootinfo->framebuffer_height;

}

void gui_update()
{
    for (int i = 0; i < width * height; i++)
        fb[i] = double_fb[i];
}

void draw_pixel_alpha(
    uint32_t x, uint32_t y,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    if (x >= width)
        return;
    if (y >= height)
        return;

    uint32_t *loc = &(double_fb[y * width + x]);

    uint32_t dst = *loc;

    uint8_t dst_r = (dst >> 16) & 0xFF;
    uint8_t dst_g = (dst >> 8) & 0xFF;
    uint8_t dst_b = dst & 0xFF;

    uint8_t inv_a = 255 - alpha;

    uint8_t out_r = (red * alpha + dst_r * inv_a) >> 8;
    uint8_t out_g = (green * alpha + dst_g * inv_a) >> 8;
    uint8_t out_b = (blue * alpha + dst_b * inv_a) >> 8;

    *loc =
        (out_r << 16) |
        (out_g << 8) |
        (out_b);
}

void draw_pixel(
    uint32_t x, uint32_t y,
    uint8_t red, uint8_t green, uint8_t blue)
{
    if (x >= width)
        return;
    if (y >= height)
        return;

    uint32_t *loc = &(double_fb[y * width + x]);

    *loc =
        (red << 16) |
        (green << 8) |
        (blue);
}

void draw_rectangle(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue)
{
    for (int i = x; i < (x + width); i++)
    {
        for (int j = y; j < (y + height); j++)
        {
            draw_pixel(i, j, red, green, blue);
        }
    }
}

void draw_rectangle_alpha(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    for (int i = x; i < (x + width); i++)
    {
        for (int j = y; j < (y + height); j++)
        {
            draw_pixel_alpha(i, j, red, green, blue, alpha);
        }
    }
}

uint32_t gui_width()
{
    return width;
}

uint32_t gui_height()
{
    return height;
}