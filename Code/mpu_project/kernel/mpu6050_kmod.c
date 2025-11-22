// SPDX-License-Identifier: GPL-2.0
/*
 * MPU6050 kernel module with char device and sysfs attributes
 * Compatible with Raspberry Pi approach (create client manually).
 * Exposes /dev/mpu6050 (read returns 6 bytes: ax,ay,az as little-endian int16)
 * Also creates sysfs attrs accel_x/y/z,temp,gyro_x/y/z
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/ktime.h>

#define DRIVER_NAME "mpu6050_kmod"
#define DEVICE_NAME "mpu6050"
#define MPU_ADDR_DEFAULT 0x68

/* Registers */
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_XOUT_H 0x3B
#define REG_TEMP_OUT_H   0x41
#define REG_GYRO_XOUT_H  0x43
#define REG_WHO_AM_I     0x75

/* Sensitivities */
#define ACCEL_SENS_2G 16384
#define GYRO_SENS_250 131

/* module params */
static int i2c_bus = 1;
module_param(i2c_bus, int, 0444);
MODULE_PARM_DESC(i2c_bus, "I2C bus (default 1)");

static int i2c_addr = MPU_ADDR_DEFAULT;
module_param(i2c_addr, int, 0444);
MODULE_PARM_DESC(i2c_addr, "I2C address (default 0x68)");

/* global */
static struct i2c_adapter *mpu_i2c_adapter;
static struct i2c_client  *mpu_i2c_client;

/* char device */
static dev_t mpu_devt;
static struct cdev mpu_cdev;
static struct class *mpu_class;
static struct mutex mpu_lock; /* protect i2c ops */

/* helper: read 8-bit reg */
static int mpu_read_reg(u8 reg)
{
    return i2c_smbus_read_byte_data(mpu_i2c_client, reg);
}

/* helper: write 8-bit reg */
static int mpu_write_reg(u8 reg, u8 val)
{
    return i2c_smbus_write_byte_data(mpu_i2c_client, reg, val);
}

/* helper: read 16-bit (big-endian) from reg_hi */
static int mpu_read16(u8 reg_hi, s16 *out)
{
    int hi, lo;
    hi = mpu_read_reg(reg_hi);
    if (hi < 0) return hi;
    lo = mpu_read_reg(reg_hi + 1);
    if (lo < 0) return lo;
    *out = (s16)((hi << 8) | lo);
    return 0;
}

/* ---------- sysfs show functions (reuse code from you) ---------- */
static inline long labs_long(long v) { return v < 0 ? -v : v; }

static ssize_t accel_x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw;
    long ms2_x1000;
    if (mpu_read16(REG_ACCEL_XOUT_H, &raw))
        return -EIO;
    ms2_x1000 = (long)raw * 9807 / ACCEL_SENS_2G;
    return sprintf(buf, "%d\t%ld.%03ld\n", (int)raw, ms2_x1000 / 1000, labs_long(ms2_x1000 % 1000));
}
static ssize_t accel_y_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw;
    long ms2_x1000;
    if (mpu_read16(REG_ACCEL_XOUT_H + 2, &raw))
        return -EIO;
    ms2_x1000 = (long)raw * 9807 / ACCEL_SENS_2G;
    return sprintf(buf, "%d\t%ld.%03ld\n", (int)raw, ms2_x1000 / 1000, labs_long(ms2_x1000 % 1000));
}
static ssize_t accel_z_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw;
    long ms2_x1000;
    if (mpu_read16(REG_ACCEL_XOUT_H + 4, &raw))
        return -EIO;
    ms2_x1000 = (long)raw * 9807 / ACCEL_SENS_2G;
    return sprintf(buf, "%d\t%ld.%03ld\n", (int)raw, ms2_x1000 / 1000, labs_long(ms2_x1000 % 1000));
}
static ssize_t temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw;
    long tmp_x100;
    if (mpu_read16(REG_TEMP_OUT_H, &raw))
        return -EIO;
    tmp_x100 = ((long)raw * 100 / 340) + 3653;
    return sprintf(buf, "%d\t%ld.%02ld\n", (int)raw, tmp_x100/100, labs_long(tmp_x100%100));
}
static ssize_t gyro_x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw; long dps_x100;
    if (mpu_read16(REG_GYRO_XOUT_H, &raw)) return -EIO;
    dps_x100 = (long)raw * 100 / GYRO_SENS_250;
    return sprintf(buf, "%d\t%ld.%02ld\n", (int)raw, dps_x100/100, labs_long(dps_x100%100));
}
static ssize_t gyro_y_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw; long dps_x100;
    if (mpu_read16(REG_GYRO_XOUT_H + 2, &raw)) return -EIO;
    dps_x100 = (long)raw * 100 / GYRO_SENS_250;
    return sprintf(buf, "%d\t%ld.%02ld\n", (int)raw, dps_x100/100, labs_long(dps_x100%100));
}
static ssize_t gyro_z_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    s16 raw; long dps_x100;
    if (mpu_read16(REG_GYRO_XOUT_H + 4, &raw)) return -EIO;
    dps_x100 = (long)raw * 100 / GYRO_SENS_250;
    return sprintf(buf, "%d\t%ld.%02ld\n", (int)raw, dps_x100/100, labs_long(dps_x100%100));
}

/* device attrs */
static DEVICE_ATTR_RO(accel_x);
static DEVICE_ATTR_RO(accel_y);
static DEVICE_ATTR_RO(accel_z);
static DEVICE_ATTR_RO(temp);
static DEVICE_ATTR_RO(gyro_x);
static DEVICE_ATTR_RO(gyro_y);
static DEVICE_ATTR_RO(gyro_z);

/* ---------- char device operations ---------- */
static ssize_t mpu_chr_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    s16 ax, ay, az;
    u8 out[6];
    int ret;
    ktime_t t0, t1;
    s64 us;

    if (count < 6)
        return -EINVAL;

    mutex_lock(&mpu_lock);
    t0 = ktime_get();
    ret = mpu_read16(REG_ACCEL_XOUT_H, &ax);
    if (!ret) mpu_read16(REG_ACCEL_XOUT_H + 2, &ay);
    if (!ret) mpu_read16(REG_ACCEL_XOUT_H + 4, &az);
    t1 = ktime_get();
    mutex_unlock(&mpu_lock);

    if (ret)
        return -EIO;

    /* pack into little-endian bytes */
    out[0] = ax & 0xFF; out[1] = (ax >> 8) & 0xFF;
    out[2] = ay & 0xFF; out[3] = (ay >> 8) & 0xFF;
    out[4] = az & 0xFF; out[5] = (az >> 8) & 0xFF;

    if (copy_to_user(buf, out, 6))
        return -EFAULT;

    /* we could expose read latency via poll/ioctl; user-space measures it too */
    us = ktime_to_us(ktime_sub(t1, t0));
    /* put latency in kernel log occasionally? commented out by default */
    /* dev_dbg(&mpu_i2c_client->dev, "i2c read latency: %lld us\n", us); */

    return 6;
}

static int mpu_chr_open(struct inode *inode, struct file *filp)
{
    filp->private_data = mpu_i2c_client;
    return 0;
}

static const struct file_operations mpu_fops = {
    .owner = THIS_MODULE,
    .open = mpu_chr_open,
    .read = mpu_chr_read,
};

/* ---------- i2c probe/remove ---------- */
/* note: Raspberry Pi setup expects probe signature with single arg when using manual client */
static int mpu_probe(struct i2c_client *client)
{
    int who, ret;

    mpu_i2c_client = client;

    who = mpu_read_reg(REG_WHO_AM_I);
    if (who < 0) {
        dev_err(&client->dev, "Failed to read WHO_AM_I\n");
        return -EIO;
    }
    dev_info(&client->dev, "WHO_AM_I = 0x%02x\n", who);

    /* wake up */
    ret = mpu_write_reg(REG_PWR_MGMT_1, 0x00);
    if (ret < 0) return ret;
    msleep(10);
    /* basic config */
    mpu_write_reg(0x19, 0x07); /* SMPLRT_DIV */
    mpu_write_reg(0x1A, 0x01); /* CONFIG */
    mpu_write_reg(0x1B, 0x00); /* GYRO_CONFIG */
    mpu_write_reg(0x1C, 0x00); /* ACCEL_CONFIG */

    /* create sysfs attrs on this device */
    device_create_file(&client->dev, &dev_attr_accel_x);
    device_create_file(&client->dev, &dev_attr_accel_y);
    device_create_file(&client->dev, &dev_attr_accel_z);
    device_create_file(&client->dev, &dev_attr_temp);
    device_create_file(&client->dev, &dev_attr_gyro_x);
    device_create_file(&client->dev, &dev_attr_gyro_y);
    device_create_file(&client->dev, &dev_attr_gyro_z);

    dev_info(&client->dev, "MPU6050 initialized OK\n");

    /* register char device dynamically */
    if (alloc_chrdev_region(&mpu_devt, 0, 1, DEVICE_NAME) < 0) {
        dev_err(&client->dev, "alloc_chrdev_region failed\n");
        return -ENOMEM;
    }
    cdev_init(&mpu_cdev, &mpu_fops);
    mpu_cdev.owner = THIS_MODULE;
    if (cdev_add(&mpu_cdev, mpu_devt, 1) < 0) {
        unregister_chrdev_region(mpu_devt, 1);
        dev_err(&client->dev, "cdev_add failed\n");
        return -EINVAL;
    }
    mpu_class = class_create(DEVICE_NAME);
    if (IS_ERR(mpu_class)) {
        cdev_del(&mpu_cdev);
        unregister_chrdev_region(mpu_devt, 1);
        dev_err(&client->dev, "class_create failed\n");
        return PTR_ERR(mpu_class);
    }
    if (!device_create(mpu_class, &client->dev, mpu_devt, NULL, DEVICE_NAME)) {
        dev_info(&client->dev, "Created /dev/%s\n", DEVICE_NAME);
    } else {
        dev_err(&client->dev, "device_create failed\n");
    }

    mutex_init(&mpu_lock);

    return 0;
}

static void mpu_remove(struct i2c_client *client)
{
    device_destroy(mpu_class, mpu_devt);
    class_destroy(mpu_class);
    cdev_del(&mpu_cdev);
    unregister_chrdev_region(mpu_devt, 1);

    device_remove_file(&client->dev, &dev_attr_accel_x);
    device_remove_file(&client->dev, &dev_attr_accel_y);
    device_remove_file(&client->dev, &dev_attr_accel_z);
    device_remove_file(&client->dev, &dev_attr_temp);
    device_remove_file(&client->dev, &dev_attr_gyro_x);
    device_remove_file(&client->dev, &dev_attr_gyro_y);
    device_remove_file(&client->dev, &dev_attr_gyro_z);

    pr_info(DRIVER_NAME ": removed\n");
}

/* i2c id table */
static const struct i2c_device_id mpu_id[] = {
    { "mpu6050", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu_id);

/* i2c_driver structure */
static struct i2c_driver mpu_driver = {
    .driver = {
        .name = DRIVER_NAME,
    },
    .probe = mpu_probe,
    .remove = mpu_remove,
    .id_table = mpu_id,
};

/* module init/exit: create client then add driver (this guarantees probe called) */
static int __init mpu_init(void)
{
    struct i2c_board_info info;
    int ret;

    pr_info(DRIVER_NAME ": init (bus=%d addr=0x%02x)\n", i2c_bus, i2c_addr);

    mpu_i2c_adapter = i2c_get_adapter(i2c_bus);
    if (!mpu_i2c_adapter) {
        pr_err(DRIVER_NAME ": cannot get I2C adapter %d\n", i2c_bus);
        return -ENODEV;
    }

    memset(&info, 0, sizeof(info));
    strscpy(info.type, "mpu6050", I2C_NAME_SIZE);
    info.addr = i2c_addr;

    mpu_i2c_client = i2c_new_client_device(mpu_i2c_adapter, &info);
    if (!mpu_i2c_client) {
        pr_err(DRIVER_NAME ": failed to create client at 0x%02x\n", i2c_addr);
        i2c_put_adapter(mpu_i2c_adapter);
        return -ENODEV;
    }

    ret = i2c_add_driver(&mpu_driver);
    i2c_put_adapter(mpu_i2c_adapter);

    if (ret) {
        pr_err(DRIVER_NAME ": failed to add driver (%d)\n", ret);
        i2c_unregister_device(mpu_i2c_client);
    }
    return ret;
}

static void __exit mpu_exit(void)
{
    i2c_unregister_device(mpu_i2c_client);
    i2c_del_driver(&mpu_driver);
    pr_info(DRIVER_NAME ": exit\n");
}

module_init(mpu_init);
module_exit(mpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You (ChatGPT-assisted)");
MODULE_DESCRIPTION("MPU6050 I2C driver + char device + sysfs");
MODULE_VERSION("1.0");
