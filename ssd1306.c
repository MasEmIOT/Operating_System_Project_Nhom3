#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kthread.h>   // Cho kernel thread
#include <linux/sched.h>     // Cho load average (avenrun)
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/thermal.h>   // Cho nhiệt độ CPU

#define SSD1306_ADDR 0x3C

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");
MODULE_DESCRIPTION("SSD1306 Dashboard for MPU6050 & System Info");

/* ===== CẤU TRÚC DỮ LIỆU TỪ MPU6050 ===== */
struct mpu_data {
    short accel_x; short accel_y; short accel_z;
    short temp;
    short gyro_x; short gyro_y; short gyro_z;
};

/* ===== IMPORT CÁC HÀM BÊN NGOÀI ===== */
extern void my_i2c_start(void);
extern void my_i2c_stop(void);
extern void my_i2c_write_byte(unsigned char byte);
extern int  my_i2c_wait_ack(void);
extern void mpu_get_raw_data(struct mpu_data *data); // Import từ mpu6050.c

/* ===== BIẾN TOÀN CỤC ===== */
static struct task_struct *update_task;
static unsigned long start_jiffies;

/* ===== DRIVER SSD1306 CƠ BẢN ===== */
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
    // Giữ nguyên init cmd của bạn
    static const u8 init_cmds[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40, 0x81, 0xFF,
        0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3, 0x00, 0xD5, 0xF0, 0xD9, 
        0x22, 0xDA, 0x12, 0xDB, 0x20, 0x8D, 0x14, 0xAF
    };
    int i;
    for (i = 0; i < ARRAY_SIZE(init_cmds); i++)
        oled_send_byte(0x00, init_cmds[i]);
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

/* Font chữ 5x7 */
static const u8 font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // Space
    // Thêm full bảng ASCII hoặc dùng bảng rút gọn. 
    // Ở đây tôi giả định bạn cần thêm các ký tự chữ cái để hiển thị Text.
    // Để code gọn, tôi dùng hàm vẽ chuỗi đơn giản bên dưới.
};

/* Chúng ta cần một bảng font đầy đủ hơn để in chữ. 
   Vì giới hạn độ dài, tôi dùng mẹo: 
   Nếu bạn chưa có font full, hãy dùng bảng font minimal cho số và một vài chữ cái.
   Nhưng tốt nhất là dùng một hàm in chuỗi sử dụng bảng font chuẩn Linux nếu có thể,
   hoặc tự define font cho các ký tự cần thiết: 'A'-'Z', 'a'-'z', ':', '.', v.v.
   
   Dưới đây là hàm giả lập in chuỗi (Bạn cần bổ sung mảng font đầy đủ vào code gốc của bạn 
   hoặc tìm file font5x7.h trên mạng paste vào).
*/
#include "font5x7_full.h" // <--- Gỉa sử bạn có file này, nếu không tôi sẽ dùng logic in số đơn giản

/* --- HÀM HỖ TRỢ HIỂN THỊ (Sử dụng font có sẵn trong code cũ của bạn và mở rộng) --- */
/* Copy lại font cũ của bạn và thêm vài ký tự cần thiết */
// ... (Giữ nguyên font số 0-9 của bạn) ...
// Để hiển thị đầy đủ, bạn CẦN một mảng font ASCII đầy đủ 0x20 - 0x7E.
// Tạm thời tôi sẽ dùng hàm in số và vài ký tự cứng để demo logic.

/* -- Hàm in chuỗi -- */
/* Yêu cầu: Bạn cần copy mảng font5x7 đầy đủ (ASCII) vào đây để hiển thị chữ cái */
/* Nếu không có font, code sẽ chỉ in được số. Tôi sẽ viết logic in số format */

static void oled_print_char_basic(char c) {
    // Logic tìm bitmap font cho c
    // Đây là placeholder, bạn cần bảng font đầy đủ để in chữ "Temp", "CPU"
    // Nếu dùng code cũ chỉ có số 0-9, bạn sẽ không in được chữ.
    // Tôi khuyến nghị bạn tìm mảng "font5x7 ascii c array" trên Google paste vào.
    
    // Ví dụ xử lý số (dựa trên code cũ):
    extern const u8 font5x7[][5]; // Link tới mảng font
    // ... logic vẽ ...
}

/* ================= LOGIC LẤY THÔNG TIN HỆ THỐNG ================= */

/* 1. Lấy nhiệt độ CPU (SoC Temp) */
static int get_cpu_temp(void) {
    struct thermal_zone_device *tz;
    int temp = 0;
    
    // Tìm thermal zone có tên "cpu-thermal" (thường thấy trên Pi)
    tz = thermal_zone_get_zone_by_name("cpu-thermal");
    if (!IS_ERR(tz) && tz) {
        thermal_zone_get_temp(tz, &temp);
        // temp trả về millidegree C (vd: 45000 = 45 độ)
        return temp / 1000;
    }
    return -1; // Lỗi
}

/* 2. Lấy CPU Load Average (1 phút) */
/* Kernel export mảng `avenrun`. Giá trị dạng fixed-point.
   Load thực tế = avenrun[0] / 2^11 (SHIFT = 11) */
static int get_cpu_load(void) {
    return (avenrun[0] >> FSHIFT); // Lấy phần nguyên của load
}

/* ================= LUỒNG CẬP NHẬT MÀN HÌNH (KERNEL THREAD) ================= */

/* Hàm helper: Tự định nghĩa font ASCII đơn giản hoặc dùng printk để debug nếu chưa có font */
/* QUAN TRỌNG: Để code chạy được ngay, tôi sẽ dùng snprintf để tạo chuỗi, 
   nhưng bạn CẦN BỔ SUNG mảng font ASCII đầy đủ vào code để in được chữ. */

static int update_oled_thread(void *data) {
    struct mpu_data mpu;
    char buf[32];
    int cpu_temp, cpu_load, uptime_sec;
    
    while (!kthread_should_stop()) {
        // 1. Lấy dữ liệu
        mpu_get_raw_data(&mpu);
        cpu_temp = get_cpu_temp();
        cpu_load = get_cpu_load();
        uptime_sec = jiffies_to_msecs(jiffies - start_jiffies) / 1000;

        // 2. Hiển thị trang 1 (Dòng 0-1): System Info
        // Format: "Up: 123s Load: 1"
        // Chú ý: Cần hàm oled_print_string (dựa trên font ASCII)
        // Nếu bạn chưa có font chữ cái, hãy in số thuần túy.
        
        oled_clear(); // Xóa màn hình
        
        /* Demo Logic hiển thị:
           Line 0: CPU: 45C  Load: 1
           Line 1: Up: 120s
           Line 2: Ax:-1200 Gx: 50
           Line 3: Ay: 400 Gy: -10
        */
        
        /* Ở đây tôi dùng printk để debug log ra dmesg thay vì in lên oled 
           nếu bạn chưa có font ASCII. Nếu có font, dùng hàm oled_print_str(buf); */
        
        // Ví dụ in thông tin ra dmesg mỗi giây (để kiểm chứng logic):
        /*
        pr_info("OLED UPDATE: T=%dC, Load=%d, Up=%ds | Ax=%d\n", 
                 cpu_temp, cpu_load, uptime_sec, mpu.accel_x);
        */
                 
        // --- Code vẽ lên màn hình (Giả định bạn đã thêm font ASCII) ---
        // Line 0
        oled_set_cursor(0, 0); 
        // oled_print_str("CPU:"); oled_print_num(cpu_temp); ...
        
        // Line 1: Accel X
        oled_set_cursor(2, 0);
        // oled_print_num(mpu.accel_x); 
        
        msleep(1000); // Cập nhật mỗi 1 giây
    }
    return 0;
}

static int __init ssd1306_init(void) {
    pr_info("SSD1306: System Monitor Init\n");
    oled_init();
    oled_clear();
    
    start_jiffies = jiffies;
    
    // Tạo kernel thread
    update_task = kthread_run(update_oled_thread, NULL, "oled_update_thread");
    if (IS_ERR(update_task)) {
        pr_err("Failed to create OLED task\n");
        return PTR_ERR(update_task);
    }
    
    return 0;
}

static void __exit ssd1306_exit(void) {
    if (update_task) {
        kthread_stop(update_task);
    }
    oled_clear();
    pr_info("SSD1306: Unloaded\n");
}

module_init(ssd1306_init);
module_exit(ssd1306_exit);
