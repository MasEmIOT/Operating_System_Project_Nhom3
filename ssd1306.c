#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/kthread.h>    // Cho kernel thread
#include <linux/kernel_stat.h> // Cho CPU stats
#include <linux/thermal.h>    // Cho nhiệt độ
#include <linux/timekeeping.h>
#include <linux/jiffies.h>
#include <linux/cpumask.h>

#define SSD1306_ADDR 0x3C

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");
MODULE_DESCRIPTION("SSD1306 System Monitor (CPU, Temp, Uptime)");

/* ===== SOFT I2C EXTERN ===== */
extern void my_i2c_start(void);
extern void my_i2c_stop(void);
extern void my_i2c_write_byte(unsigned char byte);
extern int  my_i2c_wait_ack(void);

/* ===== GLOBAL VARIABLES ===== */
static struct task_struct *monitor_thread_task; // Con trỏ quản lý luồng update

// Cấu trúc lưu trạng thái CPU cũ để tính % load
struct cpu_usage_prev {
    u64 user, nice, system, idle, iowait, irq, softirq, steal;
};
static struct cpu_usage_prev prev_stats[4]; // RPi 4 có 4 cores

/* ===== FONT 5x7 (MỞ RỘNG) ===== */
// Thêm chữ cái để hiển thị "CPU", "Temp", "Up"
static const u8 font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0: space
    {0x3E,0x51,0x49,0x45,0x3E}, // 1: 0
    {0x00,0x42,0x7F,0x40,0x00}, // 2: 1
    {0x42,0x61,0x51,0x49,0x46}, // 3: 2
    {0x21,0x41,0x45,0x4B,0x31}, // 4: 3
    {0x18,0x14,0x12,0x7F,0x10}, // 5: 4
    {0x27,0x45,0x45,0x45,0x39}, // 6: 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 7: 6
    {0x01,0x71,0x09,0x05,0x03}, // 8: 7
    {0x36,0x49,0x49,0x49,0x36}, // 9: 8
    {0x06,0x49,0x49,0x29,0x1E}, // 10: 9
    {0x00,0x00,0x24,0x00,0x00}, // 11: :
    {0x60,0x00,0x00,0x00,0x00}, // 12: . (dot)
    {0x22,0x14,0x7F,0x14,0x22}, // 13: % (gần giống)
    {0x3E,0x41,0x41,0x41,0x22}, // 14: C
    {0x7F,0x09,0x09,0x09,0x06}, // 15: P
    {0x3F,0x40,0x40,0x40,0x3F}, // 16: U
    {0x01,0x01,0x7F,0x01,0x01}, // 17: T
    {0x7F,0x49,0x49,0x49,0x41}, // 18: E
    {0x7F,0x02,0x0C,0x02,0x7F}, // 19: M
    {0x7E,0x11,0x11,0x11,0x7E}, // 20: A
    {0x1C,0x22,0x22,0x22,0x1C}, // 21: o (degree)
};

/* ===== LOW LEVEL OLED ===== */
static void oled_send_byte(u8 mode, u8 byte) {
    my_i2c_start();
    my_i2c_write_byte((SSD1306_ADDR << 1) | 0);
    my_i2c_wait_ack();
    my_i2c_write_byte(mode);
    my_i2c_wait_ack();
    my_i2c_write_byte(byte);
    my_i2c_wait_ack();
    my_i2c_stop();
}

static void oled_init(void) {
    static const u8 init_cmds[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40, 0x81, 0xFF, 0xA1, 0xA6,
        0xA8, 0x3F, 0xA4, 0xD3, 0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB,
        0x20, 0x8D, 0x14, 0xAF
    };
    int i;
    for (i = 0; i < ARRAY_SIZE(init_cmds); i++) oled_send_byte(0x00, init_cmds[i]);
}

static void oled_clear(void) {
    int i;
    for (i = 0; i < 1024; i++) oled_send_byte(0x40, 0x00);
}

static void oled_set_cursor(u8 page, u8 col) {
    oled_send_byte(0x00, 0xB0 | page);
    oled_send_byte(0x00, 0x00 | (col & 0x0F));
    oled_send_byte(0x00, 0x10 | (col >> 4));
}

static void oled_put_char(char c) {
    const u8 *b = font5x7[0]; // Default space
    if (c >= '0' && c <= '9') b = font5x7[c - '0' + 1];
    else if (c == ':') b = font5x7[11];
    else if (c == '.') b = font5x7[12];
    else if (c == '%') b = font5x7[13];
    else if (c == 'C') b = font5x7[14];
    else if (c == 'P') b = font5x7[15];
    else if (c == 'U') b = font5x7[16];
    else if (c == 'T') b = font5x7[17];
    else if (c == 'E') b = font5x7[18];
    else if (c == 'M') b = font5x7[19];
    else if (c == 'A') b = font5x7[20];
    else if (c == 'o') b = font5x7[21]; // degree

    int i;
    for (i = 0; i < 5; i++) oled_send_byte(0x40, b[i]);
    oled_send_byte(0x40, 0x00);
}

static void oled_print(const char *str) {
    while (*str) {
        oled_put_char(*str);
        str++;
    }
}

static void oled_print_num(int v) {
    if(v >= 10) oled_put_char('0' + (v / 10));
    else oled_put_char(' '); // Padding space
    oled_put_char('0' + (v % 10));
}

/* ===== SYSTEM STATS LOGIC ===== */

// 1. Hàm lấy nhiệt độ CPU
static int get_cpu_temp(void) {
    struct thermal_zone_device *tz;
    int temp = 0;
    
    // RPi thường dùng "cpu-thermal"
    tz = thermal_zone_get_zone_by_name("cpu-thermal");
    if (IS_ERR(tz) || !tz) return 0;

    thermal_zone_get_temp(tz, &temp);
    return temp / 1000; // Đổi từ mC sang độ C
}

// 2. Hàm tính % CPU Load cho từng Core
static int get_cpu_load(int cpu) {
    struct kernel_cpustat kcpustat;
    u64 user, nice, system, idle, iowait, irq, softirq, steal;
    u64 total, idle_total, total_diff, idle_diff;
    int usage = 0;

    // Lấy thống kê hiện tại
    kcpustat = kcpustat_cpu(cpu);
    user = kcpustat.cpustat[CPUTIME_USER];
    nice = kcpustat.cpustat[CPUTIME_NICE];
    system = kcpustat.cpustat[CPUTIME_SYSTEM];
    idle = kcpustat.cpustat[CPUTIME_IDLE];
    iowait = kcpustat.cpustat[CPUTIME_IOWAIT];
    irq = kcpustat.cpustat[CPUTIME_IRQ];
    softirq = kcpustat.cpustat[CPUTIME_SOFTIRQ];
    steal = kcpustat.cpustat[CPUTIME_STEAL];

    total = user + nice + system + idle + iowait + irq + softirq + steal;
    idle_total = idle + iowait;

    // Tính Delta so với lần đo trước
    total_diff = total - (prev_stats[cpu].user + prev_stats[cpu].nice + 
        prev_stats[cpu].system + prev_stats[cpu].idle + prev_stats[cpu].iowait + 
        prev_stats[cpu].irq + prev_stats[cpu].softirq + prev_stats[cpu].steal);

    idle_diff = idle_total - (prev_stats[cpu].idle + prev_stats[cpu].iowait);

    if (total_diff > 0)
        usage = 100 * (total_diff - idle_diff) / total_diff;

    // Lưu lại trạng thái để dùng cho lần sau
    prev_stats[cpu].user = user; prev_stats[cpu].nice = nice;
    prev_stats[cpu].system = system; prev_stats[cpu].idle = idle;
    prev_stats[cpu].iowait = iowait; prev_stats[cpu].irq = irq;
    prev_stats[cpu].softirq = softirq; prev_stats[cpu].steal = steal;

    return usage;
}

// 3. Hàm hiển thị toàn bộ
static void update_display_stats(void) {
    int t, load0, load1, load2, load3;
    u64 uptime_sec;

    load0 = get_cpu_load(0);
    load1 = get_cpu_load(1);
    load2 = get_cpu_load(2);
    load3 = get_cpu_load(3);
    t = get_cpu_temp();
    uptime_sec = ktime_get_boottime_seconds();

    // Dòng 0: C0:XX% C1:XX%
    oled_set_cursor(0, 0);
    oled_print("C0:"); oled_print_num(load0); oled_put_char('%');
    oled_print(" C1:"); oled_print_num(load1); oled_put_char('%');

    // Dòng 1: C2:XX% C3:XX%
    oled_set_cursor(2, 0); // SSD1306 cách dòng là page 0, 2, 4...
    oled_print("C2:"); oled_print_num(load2); oled_put_char('%');
    oled_print(" C3:"); oled_print_num(load3); oled_put_char('%');

    // Dòng 2: Temp
    oled_set_cursor(4, 0);
    oled_print("TEMP:"); oled_print_num(t); 
    oled_put_char('o'); oled_put_char('C');

    // Dòng 3: Uptime
    oled_set_cursor(6, 0);
    oled_print("UP: "); 
    oled_print_num((uptime_sec / 3600) % 100); // Giờ
    oled_put_char(':');
    oled_print_num((uptime_sec / 60) % 60);    // Phút
    oled_put_char(':');
    oled_print_num(uptime_sec % 60);           // Giây
}

/* ===== KERNEL THREAD ===== */
static int monitor_thread_fn(void *data) {
    while (!kthread_should_stop()) {
        update_display_stats();
        // Ngủ 1 giây (1000ms)
        msleep(1000);
    }
    return 0;
}

/* ===== INIT / EXIT ===== */
static int __init ssd1306_init(void) {
    pr_info("SSD1306: Monitor Driver Loaded\n");

    oled_init();
    oled_clear();

    // Khởi động thread
    monitor_thread_task = kthread_run(monitor_thread_fn, NULL, "ssd1306_mon");
    if (IS_ERR(monitor_thread_task)) {
        pr_err("SSD1306: Failed to create thread\n");
        return PTR_ERR(monitor_thread_task);
    }

    return 0;
}

static void __exit ssd1306_exit(void) {
    if (monitor_thread_task) {
        kthread_stop(monitor_thread_task);
    }
    oled_clear();
    pr_info("SSD1306: Driver Unloaded\n");
}

module_init(ssd1306_init);
module_exit(ssd1306_exit);
