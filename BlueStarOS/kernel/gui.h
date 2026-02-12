#ifndef GUI_H
#define GUI_H

#include "../bootinfo.h"
#include <stdint.h>

void init_gui(boot_info_t *bootinfo);

void draw_pixel(
    uint32_t x, uint32_t y,
    uint8_t red, uint8_t green, uint8_t blue
);

void draw_pixel_alpha(
    uint32_t x, uint32_t y,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
);

void draw_rectangle(
    int x, int y, int width, int height,
    uint8_t red, uint8_t green, uint8_t blue
);
void draw_rectangle_alpha(
    int x, int y, int width, int height,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha
);

uint32_t gui_width(void);
uint32_t gui_height(void);

void gui_update(void);

#endif
