#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "mpu6050_custom"

MODULE_LICENSE("GPL");

/* Khai báo hàm từ module soft_i2c */
extern void my_i2c_start(void);
extern void my_i2c_stop(void);
extern void my_i2c_write_byte(unsigned char byte);
extern unsigned char my_i2c_read_byte(unsigned char ack);
extern int my_i2c_wait_ack(void);

static dev_t my_dev_num;
static struct class *my_class;
static struct cdev my_cdev;

struct mpu_data {
    short accel_x; short accel_y; short accel_z;
    short temp;
    short gyro_x; short gyro_y; short gyro_z;
};

static ssize_t driver_read(struct file *file, char __user *user_buf, size_t count, loff_t *offset) {
    struct mpu_data data = {0};
    u8 buf[14];
    int i;

    /* Đọc 14 byte dữ liệu từ I2C Core */
    /* Nếu chạy trên PC, soft_i2c sẽ sinh dữ liệu giả cho cả 14 byte này */
    for(i=0; i<14; i++) {
        // 1 = ACK (đọc tiếp), byte cuối cùng thì gửi 0 = NACK
        buf[i] = my_i2c_read_byte(i < 13 ? 1 : 0);
    }

    /* --- GÁN DỮ LIỆU CHO CẢ 3 TRỤC --- */
    // Ghép High Byte và Low Byte
    data.accel_x = (buf[0] << 8) | buf[1];
    data.accel_y = (buf[2] << 8) | buf[3];
    data.accel_z = (buf[4] << 8) | buf[5];
    
    data.temp    = (buf[6] << 8) | buf[7];
    
    data.gyro_x  = (buf[8] << 8) | buf[9];
    data.gyro_y  = (buf[10] << 8) | buf[11];
    data.gyro_z  = (buf[12] << 8) | buf[13];
    /* -------------------------------- */
    
    if (copy_to_user(user_buf, &data, sizeof(data))) return -EFAULT;
    return sizeof(data);
}

static int driver_open(struct inode *inode, struct file *file) { return 0; }
static int driver_close(struct inode *inode, struct file *file) { return 0; }

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = driver_open,
    .release = driver_close,
    .read = driver_read
};

static int __init mpu_init(void) {
    alloc_chrdev_region(&my_dev_num, 0, 1, DRIVER_NAME);
    
    // Kernel 6.14 dùng 1 tham số
    my_class = class_create("mpu_class_final"); 
    
    device_create(my_class, NULL, my_dev_num, NULL, "mpu6050");
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, my_dev_num, 1);
    
    // Test kết nối I2C khi khởi động
    my_i2c_start();
    my_i2c_stop();
    
    return 0;
}

static void __exit mpu_exit(void) {
    cdev_del(&my_cdev);
    device_destroy(my_class, my_dev_num);
    class_destroy(my_class);
    unregister_chrdev_region(my_dev_num, 1);
}

module_init(mpu_init);
module_exit(mpu_exit);
