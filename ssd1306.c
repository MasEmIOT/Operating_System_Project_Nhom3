#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/timekeeping.h>
#include <linux/rtc.h>

#define SSD1306_ADDR 0x3C

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");
MODULE_DESCRIPTION("SSD1306 kernel-only driver (show date & time)");

/* ===== SOFT I2C CORE ===== */
extern void my_i2c_start(void);
extern void my_i2c_stop(void);
extern void my_i2c_write_byte(unsigned char byte);
extern int  my_i2c_wait_ack(void);

/* ===== LOW LEVEL ===== */

static void oled_send_byte(u8 mode, u8 byte)
{
    my_i2c_start();
    my_i2c_write_byte((SSD1306_ADDR << 1) | 0);
    my_i2c_wait_ack();

    my_i2c_write_byte(mode);   /* 0x00 = CMD, 0x40 = DATA */
    my_i2c_wait_ack();

    my_i2c_write_byte(byte);
    my_i2c_wait_ack();
    my_i2c_stop();
}

/* ===== INIT SSD1306 ===== */

static void oled_init(void)
{
    static const u8 init_cmds[] = {
        0xAE,
        0x20, 0x00,
        0xB0,
        0xC8,
        0x00, 0x10,
        0x40,
        0x81, 0xFF,
        0xA1,
        0xA6,
        0xA8, 0x3F,
        0xA4,
        0xD3, 0x00,
        0xD5, 0xF0,
        0xD9, 0x22,
        0xDA, 0x12,
        0xDB, 0x20,
        0x8D, 0x14,
        0xAF
    };

    int i;
    for (i = 0; i < ARRAY_SIZE(init_cmds); i++)
        oled_send_byte(0x00, init_cmds[i]);
}

/* ===== CLEAR ===== */

static void oled_clear(void)
{
    int i;
    for (i = 0; i < 1024; i++)
        oled_send_byte(0x40, 0x00);
}

/* ===== CURSOR ===== */

static void oled_set_cursor(u8 page, u8 col)
{
    oled_send_byte(0x00, 0xB0 | page);
    oled_send_byte(0x00, 0x00 | (col & 0x0F));
    oled_send_byte(0x00, 0x10 | (col >> 4));
}



static const u8 font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
};

static void oled_put_char(char c)
{
    const u8 *b;
    int i;

    if (c >= '0' && c <= '9')
        b = font5x7[c - '0' + 1];
    else
        b = font5x7[0];

    for (i = 0; i < 5; i++)
        oled_send_byte(0x40, b[i]);

    oled_send_byte(0x40, 0x00);
}

static void oled_print_2d(int v)
{
    oled_put_char('0' + (v / 10));
    oled_put_char('0' + (v % 10));
}

/* ===== SHOW DATE & TIME ===== */

static void oled_show_datetime(void)
{
    struct timespec64 ts;
    struct rtc_time tm;
    int year;

    ktime_get_real_ts64(&ts);
    rtc_time64_to_tm(ts.tv_sec, &tm);
    year = tm.tm_year + 1900;

    oled_clear();

    /* Line 1: YYYY MM DD */
    oled_set_cursor(0, 0);
    oled_print_2d(year / 100);
    oled_print_2d(year % 100);
    oled_put_char(' ');
    oled_print_2d(tm.tm_mon + 1);
    oled_put_char(' ');
    oled_print_2d(tm.tm_mday);

    /* Line 2: HH MM SS */
    oled_set_cursor(2, 0);
    oled_print_2d(tm.tm_hour);
    oled_put_char(' ');
    oled_print_2d(tm.tm_min);
    oled_put_char(' ');
    oled_print_2d(tm.tm_sec);
}

/* ===== MODULE INIT / EXIT ===== */

static int __init ssd1306_init(void)
{
    pr_info("SSD1306: kernel module loaded\n");

    oled_init();
    msleep(50);
    oled_show_datetime();

    return 0;
}

static void __exit ssd1306_exit(void)
{
    oled_clear();
    pr_info("SSD1306: kernel module unloaded\n");
}

module_init(ssd1306_init);
module_exit(ssd1306_exit);
