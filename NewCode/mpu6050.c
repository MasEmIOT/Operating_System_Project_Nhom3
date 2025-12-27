#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define DRIVER_NAME "mpu6050_custom"
#define MPU_ADDR 0x68      // Địa chỉ I2C mặc định của MPU6050
#define PWR_MGMT_1 0x6B    // Thanh ghi quản lý nguồn
#define ACCEL_XOUT_H 0x3B  // Thanh ghi bắt đầu chứa dữ liệu đo

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");

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

/* Hàm phụ trợ: Ghi 1 byte vào 1 thanh ghi của MPU6050 */
static void mpu_write_reg(u8 reg, u8 value) {
    my_i2c_start();
    my_i2c_write_byte(MPU_ADDR << 1 | 0); // Gửi địa chỉ + Bit Write (0)
    if (!my_i2c_wait_ack()) {
        // pr_err("MPU6050: No ACK at Write Addr\n"); // Debug
    }
    
    my_i2c_write_byte(reg); // Chọn thanh ghi
    my_i2c_wait_ack();
    
    my_i2c_write_byte(value); // Ghi giá trị
    my_i2c_wait_ack();
    
    my_i2c_stop();
}

static ssize_t driver_read(struct file *file, char __user *user_buf, size_t count, loff_t *offset) {
    struct mpu_data data = {0};
    u8 buf[14];
    int i;

    /* QUY TRÌNH ĐỌC CHUẨN I2C: */
    
    /* BƯỚC 1: Báo cho cảm biến biết muốn đọc từ thanh ghi 0x3B trở đi */
    my_i2c_start();
    my_i2c_write_byte(MPU_ADDR << 1 | 0); // Write Mode
    my_i2c_wait_ack();
    my_i2c_write_byte(ACCEL_XOUT_H);      // Trỏ con trỏ vào thanh ghi 0x3B
    my_i2c_wait_ack();
    
    /* BƯỚC 2: Khởi động lại (Repeat Start) để chuyển sang chế độ Đọc */
    my_i2c_start();
    my_i2c_write_byte(MPU_ADDR << 1 | 1); // Read Mode (Bit cuối là 1)
    my_i2c_wait_ack();

    /* BƯỚC 3: Đọc liên tiếp 14 byte */
    for(i=0; i<14; i++) {
        // Nếu chưa phải byte cuối -> Gửi ACK (1) để đọc tiếp
        // Nếu là byte cuối (i=13) -> Gửi NACK (0) để dừng
        buf[i] = my_i2c_read_byte(i < 13 ? 1 : 0);
    }
    
    my_i2c_stop(); // Kết thúc giao tiếp

    /* --- GÁN DỮ LIỆU --- */
    data.accel_x = (buf[0] << 8) | buf[1];
    data.accel_y = (buf[2] << 8) | buf[3];
    data.accel_z = (buf[4] << 8) | buf[5];
    data.temp    = (buf[6] << 8) | buf[7];
    data.gyro_x  = (buf[8] << 8) | buf[9];
    data.gyro_y  = (buf[10] << 8) | buf[11];
    data.gyro_z  = (buf[12] << 8) | buf[13];
    
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
    if (alloc_chrdev_region(&my_dev_num, 0, 1, DRIVER_NAME) < 0) return -1;
    
    my_class = class_create("mpu_class_final"); 
    device_create(my_class, NULL, my_dev_num, NULL, "mpu6050");
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, my_dev_num, 1);
    
    /* QUAN TRỌNG: Đánh thức MPU6050 khỏi chế độ ngủ */
    // Ghi 0x00 vào thanh ghi 0x6B (PWR_MGMT_1)
    mpu_write_reg(PWR_MGMT_1, 0x00);
    pr_info("MPU6050: Init & Waked Up!\n");
    
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
