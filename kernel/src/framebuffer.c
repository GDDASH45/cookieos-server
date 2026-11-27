/*
 * CookieOS Server 2026 - UEFI GOP framebuffer console
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stddef.h>
#include <stdint.h>
#include <bootinfo.h>
#include "framebuffer.h"

#define FONT_SCALE 2U
#define CELL_WIDTH 12U
#define CELL_HEIGHT 16U

static volatile uint8_t *fb;
static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_pitch;
static uint32_t fb_format;
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint32_t foreground = 0x00D8E8F0U;
static uint32_t background = 0x00000000U;

static const uint8_t *glyph(char c)
{
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
    };

    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return letters[(unsigned int)(c - 'A')];
    if (c >= '0' && c <= '9')
        return digits[(unsigned int)(c - '0')];

    switch (c) {
    case '-': { static const uint8_t g[7]={0,0,0,31,0,0,0}; return g; }
    case '_': { static const uint8_t g[7]={0,0,0,0,0,0,31}; return g; }
    case '.': { static const uint8_t g[7]={0,0,0,0,0,12,12}; return g; }
    case ':': { static const uint8_t g[7]={0,12,12,0,12,12,0}; return g; }
    case '/': { static const uint8_t g[7]={1,2,2,4,8,8,16}; return g; }
    case '[': { static const uint8_t g[7]={14,8,8,8,8,8,14}; return g; }
    case ']': { static const uint8_t g[7]={14,2,2,2,2,2,14}; return g; }
    case '(': { static const uint8_t g[7]={2,4,8,8,8,4,2}; return g; }
    case ')': { static const uint8_t g[7]={8,4,2,2,2,4,8}; return g; }
    case ',': { static const uint8_t g[7]={0,0,0,0,4,4,8}; return g; }
    case '=': { static const uint8_t g[7]={0,31,0,31,0,0,0}; return g; }
    case '+': { static const uint8_t g[7]={0,4,4,31,4,4,0}; return g; }
    case '*': { static const uint8_t g[7]={0,17,10,31,10,17,0}; return g; }
    default: return blank;
    }
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    volatile uint8_t *p;
    if (x >= fb_width || y >= fb_height)
        return;
    p = fb + ((size_t)y * fb_pitch + x) * 4U;
    if (fb_format == 0U) {
        p[0] = (uint8_t)(color >> 16);
        p[1] = (uint8_t)(color >> 8);
        p[2] = (uint8_t)color;
    } else {
        p[0] = (uint8_t)color;
        p[1] = (uint8_t)(color >> 8);
        p[2] = (uint8_t)(color >> 16);
    }
    p[3] = 0;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color)
{
    uint32_t dx, dy;
    for (dy = 0; dy < h && y + dy < fb_height; ++dy)
        for (dx = 0; dx < w && x + dx < fb_width; ++dx)
            put_pixel(x + dx, y + dy, color);
}

static void scroll(void)
{
    uint32_t y, x;
    if (fb_height <= CELL_HEIGHT)
        return;
    for (y = 0; y < fb_height - CELL_HEIGHT; ++y) {
        for (x = 0; x < fb_width; ++x) {
            volatile uint8_t *dst = fb + ((size_t)y * fb_pitch + x) * 4U;
            volatile uint8_t *src = fb + ((size_t)(y + CELL_HEIGHT) * fb_pitch + x) * 4U;
            dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=src[3];
        }
    }
    fill_rect(0, fb_height - CELL_HEIGHT, fb_width, CELL_HEIGHT, background);
    cursor_y = fb_height - CELL_HEIGHT;
}

static void newline(void)
{
    cursor_x = 8;
    cursor_y += CELL_HEIGHT;
    if (cursor_y + CELL_HEIGHT > fb_height)
        scroll();
}

void framebuffer_init(const COOKIEOS_BOOT_INFO *info)
{
    size_t required;
    if (!info || !info->framebuffer_base || !info->framebuffer_width ||
        !info->framebuffer_height ||
        info->framebuffer_pixels_per_scanline < info->framebuffer_width ||
        (info->framebuffer_pixel_format != 0U &&
         info->framebuffer_pixel_format != 1U))
        return;

    required = (size_t)info->framebuffer_pixels_per_scanline *
               info->framebuffer_height * 4U;
    if (info->framebuffer_size < required)
        return;

    fb = (volatile uint8_t *)(uintptr_t)info->framebuffer_base;
    fb_width = info->framebuffer_width;
    fb_height = info->framebuffer_height;
    fb_pitch = info->framebuffer_pixels_per_scanline;
    fb_format = info->framebuffer_pixel_format;
    cursor_x = 8;
    cursor_y = 8;
    framebuffer_clear();
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * Draw an uncompressed 24-bit or 32-bit Windows BMP, exactly 128x128.
 * The image is centered on the GOP framebuffer.
 */
int framebuffer_draw_bmp(const void *data, size_t size)
{
    const uint8_t *bmp = (const uint8_t *)data;
    uint32_t pixel_offset, dib_size, compression, row_size;
    uint32_t x, y, bytes_per_pixel;
    int32_t width, height;
    uint16_t planes, bpp;
    uint32_t abs_height;
    uint32_t origin_x, origin_y;

    if (!fb || !bmp || size < 54U || bmp[0] != 'B' || bmp[1] != 'M')
        return 0;

    pixel_offset = read_le32(bmp + 10);
    dib_size = read_le32(bmp + 14);
    if (dib_size < 40U || (size_t)14U + dib_size > size)
        return 0;

    width = (int32_t)read_le32(bmp + 18);
    height = (int32_t)read_le32(bmp + 22);
    planes = read_le16(bmp + 26);
    bpp = read_le16(bmp + 28);
    compression = read_le32(bmp + 30);

    if (width != 128 || height == 0 || height == (-128 - 1) ||
        (height != 128 && height != -128) || planes != 1U ||
        (bpp != 24U && bpp != 32U) || compression != 0U)
        return 0;

    bytes_per_pixel = bpp / 8U;
    row_size = (128U * bytes_per_pixel + 3U) & ~3U;
    if ((size_t)pixel_offset > size ||
        (size_t)row_size > (size - (size_t)pixel_offset) / 128U)
        return 0;

    abs_height = height < 0 ? (uint32_t)(-height) : (uint32_t)height;
    if (abs_height != 128U)
        return 0;

    origin_x = fb_width > 128U ? (fb_width - 128U) / 2U : 0U;
    origin_y = fb_height > 128U ? (fb_height - 128U) / 2U : 0U;

    for (y = 0; y < 128U; ++y) {
        uint32_t source_y = height > 0 ? 127U - y : y;
        const uint8_t *row = bmp + pixel_offset +
                             (size_t)source_y * row_size;
        for (x = 0; x < 128U; ++x) {
            const uint8_t *pixel = row + (size_t)x * bytes_per_pixel;
            uint32_t color = ((uint32_t)pixel[2] << 16) |
                             ((uint32_t)pixel[1] << 8) |
                             (uint32_t)pixel[0];
            put_pixel(origin_x + x, origin_y + y, color);
        }
    }

    return 1;
}

void framebuffer_clear(void)
{
    if (!fb)
        return;
    fill_rect(0, 0, fb_width, fb_height, 0x00000000U);
    cursor_x = 8;
    cursor_y = 8;
}

int framebuffer_available(void)
{
    return fb != 0;
}

void framebuffer_putc(char c)
{
    const uint8_t *rows;
    uint32_t row, col, sx, sy;

    if (!fb)
        return;
    if (c == '\r')
        return;
    if (c == '\n') {
        newline();
        return;
    }
    if (cursor_x + CELL_WIDTH > fb_width)
        newline();
    rows = glyph(c);
    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 5; ++col) {
            if (!(rows[row] & (1U << (4U - col))))
                continue;
            for (sy = 0; sy < FONT_SCALE; ++sy)
                for (sx = 0; sx < FONT_SCALE; ++sx)
                    put_pixel(cursor_x + col * FONT_SCALE + sx,
                              cursor_y + row * FONT_SCALE + sy,
                              foreground);
        }
    }
    cursor_x += CELL_WIDTH;
}

void framebuffer_write(const char *s)
{
    if (!s)
        return;
    while (*s)
        framebuffer_putc(*s++);
}

void framebuffer_write_hex(unsigned long long value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    framebuffer_write("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        framebuffer_putc(digits[(value >> shift) & 15U]);
}

void framebuffer_write_dec(unsigned long long value)
{
    char buf[21];
    unsigned int n = 0;
    if (!value) {
        framebuffer_putc('0');
        return;
    }
    while (value && n < sizeof(buf)) {
        buf[n++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (n)
        framebuffer_putc(buf[--n]);
}
