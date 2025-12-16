#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>

#define DRIVER_NAME "ssd1306_driver"
#define SSD1306_ADDR 0x3C 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");

/* KẾT NỐI VỚI CORE I2C TỰ VIẾT */
extern void my_i2c_start(void);
extern void my_i2c_stop(void);
extern void my_i2c_write_byte(unsigned char byte);
extern int my_i2c_wait_ack(void);
/* --------------------------- */

static struct class *my_class;
static dev_t my_dev_num;
static struct cdev my_cdev;

/* Hàm gửi 1 byte lệnh hoặc dữ liệu qua Soft I2C */
/* mode: 0x00 = Command, 0x40 = Data */
static void oled_send_byte(u8 mode, u8 byte) {
    my_i2c_start();
    my_i2c_write_byte(SSD1306_ADDR << 1 | 0); // Địa chỉ + Write
    my_i2c_wait_ack();
    
    my_i2c_write_byte(mode); // Control Byte
    my_i2c_wait_ack();
    
    my_i2c_write_byte(byte); // Data/Command
    my_i2c_wait_ack();
    
    my_i2c_stop();
}

static void oled_init(void) {
    // Chuỗi khởi tạo chuẩn cho SSD1306 128x64
    u8 init_cmds[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40, 
        0x81, 0xFF, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3, 
        0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB, 
        0x20, 0x8D, 0x14, 0xAF
    };
    int i;
    for(i=0; i<sizeof(init_cmds); i++) {
        oled_send_byte(0x00, init_cmds[i]);
    }
}

static ssize_t driver_write(struct file *file, const char __user *user_buf, size_t count, loff_t *offset) {
    u8 *buf;
    int i;
    
    buf = kmalloc(count, GFP_KERNEL);
    if (!buf) return -ENOMEM;
    
    if(copy_from_user(buf, user_buf, count)) { 
        kfree(buf); 
        return -EFAULT; 
    }

    // Gửi liên tục dữ liệu ra màn hình
    // Tối ưu hóa: Gửi Start -> Addr -> Mode -> Data... -> Data -> Stop
    my_i2c_start();
    my_i2c_write_byte(SSD1306_ADDR << 1 | 0);
    my_i2c_wait_ack();
    my_i2c_write_byte(0x40); // Báo hiệu các byte sau là Data (RAM)
    my_i2c_wait_ack();

    for(i=0; i<count; i++) {
        my_i2c_write_byte(buf[i]);
        my_i2c_wait_ack();
    }
    
    my_i2c_stop();
    
    kfree(buf);
    return count;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = driver_write,
};

static int __init oled_driver_init(void) {
    // Tạo Device File
    if (alloc_chrdev_region(&my_dev_num, 0, 1, DRIVER_NAME) < 0) return -1;
    
    my_class = class_create("oled_class_final");
    device_create(my_class, NULL, my_dev_num, NULL, "ssd1306");
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, my_dev_num, 1);
    
    // Khởi tạo màn hình qua Soft I2C
    oled_init();
    pr_info("SSD1306: Driver Loaded (Using Soft I2C)\n");
    
    return 0;
}

static void __exit oled_driver_exit(void) {
    cdev_del(&my_cdev);
    device_destroy(my_class, my_dev_num);
    class_destroy(my_class);
    unregister_chrdev_region(my_dev_num, 1);
}

module_init(oled_driver_init);
module_exit(oled_driver_exit);
