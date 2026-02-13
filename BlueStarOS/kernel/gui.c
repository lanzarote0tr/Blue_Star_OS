#include "gui.h"
#include "heap.h"
#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t *fb;
static uint32_t *double_fb;
static uint32_t *render_fb;
static uint32_t width;
static uint32_t height;
static uint32_t fb_pitch_pixels;
static uint32_t render_pitch_pixels;
static uint8_t red_shift;
static uint8_t green_shift;
static uint8_t blue_shift;
static int use_direct_fb;
static int dirty_valid;
static uint32_t dirty_x0;
static uint32_t dirty_y0;
static uint32_t dirty_x1;
static uint32_t dirty_y1;

static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static inline uint32_t make_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << red_shift) |
           ((uint32_t)g << green_shift) |
           ((uint32_t)b << blue_shift);
}

static inline uint8_t channel_from_pixel(uint32_t pixel, uint8_t shift)
{
    return (uint8_t)((pixel >> shift) & 0xFFU);
}

static int clip_rect(int *x0, int *y0, int *x1, int *y1)
{
    if (*x1 <= 0 || *y1 <= 0) return 0;
    if (*x0 >= (int)width || *y0 >= (int)height) return 0;

    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > (int)width) *x1 = (int)width;
    if (*y1 > (int)height) *y1 = (int)height;

    return (*x0 < *x1) && (*y0 < *y1);
}

static inline void mark_dirty_rect(int x0, int y0, int x1, int y1)
{
    if (!clip_rect(&x0, &y0, &x1, &y1)) {
        return;
    }

    if (!dirty_valid) {
        dirty_valid = 1;
        dirty_x0 = (uint32_t)x0;
        dirty_y0 = (uint32_t)y0;
        dirty_x1 = (uint32_t)x1;
        dirty_y1 = (uint32_t)y1;
        return;
    }

    if ((uint32_t)x0 < dirty_x0) dirty_x0 = (uint32_t)x0;
    if ((uint32_t)y0 < dirty_y0) dirty_y0 = (uint32_t)y0;
    if ((uint32_t)x1 > dirty_x1) dirty_x1 = (uint32_t)x1;
    if ((uint32_t)y1 > dirty_y1) dirty_y1 = (uint32_t)y1;
}

static inline void mark_dirty(int x0, int y0, int x1, int y1)
{
    if (use_direct_fb) {
        return;
    }
    mark_dirty_rect(x0, y0, x1, y1);
}

static inline void copy_pixels_sse(uint32_t *dst, const uint32_t *src, uint32_t count)
{
    uint32_t x = 0;

    for (; x + 15 < count; x += 16)
    {
        __m128i d0 = _mm_loadu_si128((const __m128i *)&src[x]);
        __m128i d1 = _mm_loadu_si128((const __m128i *)&src[x + 4]);
        __m128i d2 = _mm_loadu_si128((const __m128i *)&src[x + 8]);
        __m128i d3 = _mm_loadu_si128((const __m128i *)&src[x + 12]);
        _mm_storeu_si128((__m128i *)&dst[x], d0);
        _mm_storeu_si128((__m128i *)&dst[x + 4], d1);
        _mm_storeu_si128((__m128i *)&dst[x + 8], d2);
        _mm_storeu_si128((__m128i *)&dst[x + 12], d3);
    }

    for (; x + 3 < count; x += 4)
    {
        __m128i d = _mm_loadu_si128((const __m128i *)&src[x]);
        _mm_storeu_si128((__m128i *)&dst[x], d);
    }

    for (; x < count; x++)
    {
        dst[x] = src[x];
    }
}

void init_gui(boot_info_t *bootinfo)
{
    if (bootinfo == NULL) {
        halt_forever();
    }

    fb = (uint32_t *)(uintptr_t)bootinfo->framebuffer_base;
    width = bootinfo->framebuffer_width;
    height = bootinfo->framebuffer_height;
    fb_pitch_pixels = bootinfo->framebuffer_pitch / (uint32_t)sizeof(uint32_t);
    if (fb_pitch_pixels == 0 || fb_pitch_pixels < width) {
        fb_pitch_pixels = width;
    }

    if (bootinfo->framebuffer_format == FB_FORMAT_RGBX) {
        red_shift = 0;
        green_shift = 8;
        blue_shift = 16;
    } else {
        red_shift = 16;
        green_shift = 8;
        blue_shift = 0;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0) {
        halt_forever();
    }

    double_fb = malloc((size_t)(pixel_count * sizeof(uint32_t)));
    if (double_fb != NULL) {
        for (uint64_t i = 0; i < pixel_count; i++) {
            double_fb[i] = 0;
        }

        render_fb = double_fb;
        render_pitch_pixels = width;
        use_direct_fb = 0;
        dirty_valid = 1;
        dirty_x0 = 0;
        dirty_y0 = 0;
        dirty_x1 = width;
        dirty_y1 = height;
        return;
    }

    /* Fallback for low-memory systems: render directly into the GOP framebuffer. */
    render_fb = fb;
    render_pitch_pixels = fb_pitch_pixels;
    use_direct_fb = 1;
    dirty_valid = 0;
}

void gui_update(void)
{
    if (use_direct_fb) {
        return;
    }

    if (!dirty_valid) {
        return;
    }

    uint32_t x0 = dirty_x0;
    uint32_t y0 = dirty_y0;
    uint32_t x1 = dirty_x1;
    uint32_t y1 = dirty_y1;
    dirty_valid = 0;

    uint32_t span = x1 - x0;
    uint32_t *src_row = &render_fb[(uint64_t)y0 * (uint64_t)render_pitch_pixels + (uint64_t)x0];
    uint32_t *dst_row = &fb[(uint64_t)y0 * (uint64_t)fb_pitch_pixels + (uint64_t)x0];

    uint32_t rows = y1 - y0;
    for (uint32_t row = 0; row < rows; row++)
    {
        copy_pixels_sse(dst_row, src_row, span);
        src_row += render_pitch_pixels;
        dst_row += fb_pitch_pixels;
    }
}

void draw_pixel_alpha(
    uint32_t x, uint32_t y,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    if (x >= width)
        return;
    if (y >= height)
        return;

    uint32_t *loc = &(render_fb[(uint64_t)y * (uint64_t)render_pitch_pixels + (uint64_t)x]);

    uint32_t dst = *loc;

    uint8_t dst_r = channel_from_pixel(dst, red_shift);
    uint8_t dst_g = channel_from_pixel(dst, green_shift);
    uint8_t dst_b = channel_from_pixel(dst, blue_shift);

    uint8_t inv_a = 255 - alpha;

    uint8_t out_r = (uint8_t)((red * alpha + dst_r * inv_a) >> 8);
    uint8_t out_g = (uint8_t)((green * alpha + dst_g * inv_a) >> 8);
    uint8_t out_b = (uint8_t)((blue * alpha + dst_b * inv_a) >> 8);

    *loc = make_pixel(out_r, out_g, out_b);

    mark_dirty((int)x, (int)y, (int)x + 1, (int)y + 1);
}

void draw_pixel(
    uint32_t x, uint32_t y,
    uint8_t red, uint8_t green, uint8_t blue)
{
    if (x >= width)
        return;
    if (y >= height)
        return;

    uint32_t *loc = &(render_fb[(uint64_t)y * (uint64_t)render_pitch_pixels + (uint64_t)x]);

    *loc = make_pixel(red, green, blue);

    mark_dirty((int)x, (int)y, (int)x + 1, (int)y + 1);
}

void draw_rectangle(int x, int y, int rect_width, int rect_height, uint8_t red, uint8_t green, uint8_t blue)
{
    if (rect_width <= 0 || rect_height <= 0) {
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + rect_width;
    int y1 = y + rect_height;
    if (!clip_rect(&x0, &y0, &x1, &y1)) {
        return;
    }

    int clipped_width = x1 - x0;
    int clipped_height = y1 - y0;
    uint32_t color = make_pixel(red, green, blue);
    __m128i color128 = _mm_set1_epi32((int)color);

    uint32_t *row = &render_fb[(uint64_t)y0 * (uint64_t)render_pitch_pixels + (uint64_t)x0];
    for (int j = 0; j < clipped_height; j++)
    {
        int i = 0;

        for (; i + 15 < clipped_width; i += 16)
        {
            _mm_storeu_si128((__m128i*)&row[i], color128);
            _mm_storeu_si128((__m128i*)&row[i + 4], color128);
            _mm_storeu_si128((__m128i*)&row[i + 8], color128);
            _mm_storeu_si128((__m128i*)&row[i + 12], color128);
        }

        for (; i + 3 < clipped_width; i += 4)
        {
            _mm_storeu_si128((__m128i*)&row[i], color128);
        }

        for (; i < clipped_width; i++)
        {
            row[i] = color;
        }

        row += render_pitch_pixels;
    }

    mark_dirty(x0, y0, x1, y1);
}

void draw_rectangle_alpha(int x, int y, int rect_width, int rect_height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    if (rect_width <= 0 || rect_height <= 0) {
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + rect_width;
    int y1 = y + rect_height;
    if (!clip_rect(&x0, &y0, &x1, &y1)) {
        return;
    }

    uint32_t src_r = (uint32_t)red;
    uint32_t src_g = (uint32_t)green;
    uint32_t src_b = (uint32_t)blue;
    uint32_t a = (uint32_t)alpha;
    uint32_t inv_a = 255u - a;

    int clipped_width = x1 - x0;
    int clipped_height = y1 - y0;
    uint32_t *row = &render_fb[(uint64_t)y0 * (uint64_t)render_pitch_pixels + (uint64_t)x0];

    for (int j = 0; j < clipped_height; j++)
    {
        for (int i = 0; i < clipped_width; i++)
        {
            uint32_t dst = row[i];
            uint32_t dst_r = (dst >> red_shift) & 0xFFu;
            uint32_t dst_g = (dst >> green_shift) & 0xFFu;
            uint32_t dst_b = (dst >> blue_shift) & 0xFFu;

            uint32_t out_r = ((src_r * a) + (dst_r * inv_a)) >> 8;
            uint32_t out_g = ((src_g * a) + (dst_g * inv_a)) >> 8;
            uint32_t out_b = ((src_b * a) + (dst_b * inv_a)) >> 8;
            row[i] = (out_r << red_shift) | (out_g << green_shift) | (out_b << blue_shift);
        }
        row += render_pitch_pixels;
    }

    mark_dirty(x0, y0, x1, y1);
}

uint32_t gui_width(void)
{
    return width;
}

uint32_t gui_height(void)
{
    return height;
}
