// test_ssd1306_write.c
// OLED Static Text: "Welcome"
// Build: gcc -O2 -o oled_static test_ssd1306_write.c

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define OLED_W 128
#define OLED_H 64
#define OLED_SIZE (OLED_W * OLED_H / 8)

#define FONT_W 6
#define FONT_H 8

// =================================================================================
//  FONT 6x8
// =================================================================================
static const uint8_t font6x8[96][6] = {
#include "font6x8_basic.inc"
};

// =================================================================================
//  DRAW PIXEL
// =================================================================================
static void draw_pixel(uint8_t *buf, int x, int y)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    int idx = (y / 8) * OLED_W + x;
    buf[idx] |= (1 << (y & 7));
}

// =================================================================================
//  DRAW CHAR (scale)
// =================================================================================
static void draw_char_scaled(uint8_t *buf, int x, int y, char c, float scale)
{
    if (c < 32 || c > 127) return;
    int id = c - 32;

    for (int col = 0; col < 6; col++)
    {
        uint8_t line = font6x8[id][col];

        for (int row = 0; row < 8; row++)
        {
            if (line & (1 << (row)))
            {
                for (int sx = 0; sx < scale; sx++)
                    for (int sy = 0; sy < scale; sy++)
                        draw_pixel(buf,
                                   x + col * scale + sx,
                                   y + row * scale + sy);
            }
        }
    }
}

// =================================================================================
//  DRAW TEXT
// =================================================================================
static void draw_text_scaled(uint8_t *buf, int x, int y, const char *s, float scale)
{
    while (*s)
    {
        draw_char_scaled(buf, x, y, *s, scale);
        x += FONT_W * scale;
        s++;
    }
}

// =================================================================================
//  CLEAR BUFFER
// =================================================================================
static void clear_oled(uint8_t *buf)
{
    memset(buf, 0x00, OLED_SIZE);
}

// =================================================================================
//  MAIN SHOW STATIC TEXT
// =================================================================================
int main()
{
    int oled = open("/dev/ssd1306", O_WRONLY);
    if (oled < 0) { perror("open /dev/ssd1306"); return 1; }

    const char *msg = "WELCOME";

    uint8_t buf[OLED_SIZE];

    // You can adjust scale or position here
    float scale = 2.0;            // text size (2x bigger)
    int x = 10;                   // X position
    int y = 20;                   // Y position

    while (1)
    {
        clear_oled(buf);

        draw_text_scaled(buf, x, y, msg, scale);

        write(oled, buf, OLED_SIZE);

        usleep(100000);   // 10 FPS to keep refreshing
    }

    close(oled);
    return 0;
}
