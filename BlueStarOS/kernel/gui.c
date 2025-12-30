#include "gui.h"
#include <emmintrin.h>

uint32_t *fb;
uint32_t double_fb[2560 * 1440 * 2];
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
    uint32_t *src = double_fb;
    uint32_t *dst = fb;

    size_t scanline = width;  // 한 줄 픽셀 수
    size_t y;

    for (y = 0; y < height; y++)
    {
        size_t x = 0;
        size_t base = y * scanline;

        // 4픽셀씩 SSE2로 복사, 루프 언롤링 2회분
        for (; x + 7 < scanline; x += 8)
        {
            __m128i d1 = _mm_load_si128((__m128i*)&src[base + x]);
            __m128i d2 = _mm_load_si128((__m128i*)&src[base + x + 4]);

            _mm_store_si128((__m128i*)&dst[base + x], d1);
            _mm_store_si128((__m128i*)&dst[base + x + 4], d2);
        }

        // 남은 픽셀 처리
        for (; x < scanline; x++)
        {
            dst[base + x] = src[base + x];
        }
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

// assuming 32-bit pixel format: 0x00RRGGBB
static inline uint32_t make_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void draw_rectangle(int x, int y, int rect_width, int rect_height, uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t color = make_pixel(red, green, blue);
    __m128i color128 = _mm_set1_epi32(color); // 4픽셀용
    __m128i color128_2 = color128;            // 루프 언롤링 두 번째 블록용

    for (int j = y; j < y + rect_height; j++)
    {
        uint32_t *row = &double_fb[j * width + x];
        int i = 0;

        // 8픽셀 단위 루프 언롤링 (4픽셀씩 2번)
        for (; i + 7 < rect_width; i += 8)
        {
            _mm_storeu_si128((__m128i*)&row[i], color128);       // 4픽셀
            _mm_storeu_si128((__m128i*)&row[i + 4], color128_2); // 다음 4픽셀
        }

        // 남은 픽셀 처리
        for (; i < rect_width; i++)
        {
            row[i] = color;
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