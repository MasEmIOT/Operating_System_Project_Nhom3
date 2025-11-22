// ssd1306_i2c.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define DRIVER_NAME "ssd1306_i2c"
#define DEVICE_NAME "ssd1306"
#define SSD1306_I2C_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 8)
#define MAX_TRANSFER 32  // chunk size for i2c writes (including control byte)

static dev_t dev_number;
static struct class *ssd1306_class;
static struct cdev ssd1306_cdev;
static struct i2c_client *ssd1306_client;

static int ssd1306_send_command(struct i2c_client *client, const u8 *cmds, int len)
{
    int ret;
    u8 buf[1 + 32]; // control + chunk
    int sent = 0;

    while (sent < len) {
        int chunk = min(len - sent, 32);
        buf[0] = 0x00; // control byte: Co = 0, D/C# = 0 (command)
        memcpy(&buf[1], &cmds[sent], chunk);
        ret = i2c_master_send(client, buf, chunk + 1);
        if (ret < 0) return ret;
        sent += chunk;
    }
    return 0;
}

static int ssd1306_send_data(struct i2c_client *client, const u8 *data, int len)
{
    int ret;
    u8 buf[1 + MAX_TRANSFER];
    int sent = 0;

    while (sent < len) {
        int chunk = min(len - sent, MAX_TRANSFER);
        buf[0] = 0x40; // control byte: Co = 0, D/C# = 1 (data)
        memcpy(&buf[1], &data[sent], chunk);
        ret = i2c_master_send(client, buf, chunk + 1);
        if (ret < 0) return ret;
        sent += chunk;
    }
    return 0;
}

static int ssd1306_init_display(struct i2c_client *client)
{
    // Typical init sequence for 128x64 SSD1306
    u8 init_cmds[] = {
        0xAE, // display off
        0xD5, 0x80, // set display clock divide ratio/oscillator freq
        0xA8, 0x3F, // multiplex 0x3F for 64 rows
        0xD3, 0x00, // display offset
        0x40, // start line = 0
        0x8D, 0x14, // charge pump (enable)
        0x20, 0x00, // memory addressing mode = horizontal
        0xA1, // segment remap (column address 127 mapped to SEG0)
        0xC8, // COM output scan direction remapped (COM scan dec)
        0xDA, 0x12, // COM pins hardware config
        0x81, 0x7F, // contrast
        0xD9, 0xF1, // pre-charge period
        0xDB, 0x40, // Vcomh deselect level
        0xA4, // entire display ON (resume)
        0xA6, // normal display (A7 for inverse)
        0xAF // display ON
    };
    return ssd1306_send_command(client, init_cmds, sizeof(init_cmds));
}

static ssize_t ssd1306_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    u8 *kbuf;
    int ret;

    if (count != BUFFER_SIZE) {
        pr_warn(DRIVER_NAME ": expected %d bytes (framebuffer), got %zu\n", BUFFER_SIZE, count);
        return -EINVAL;
    }

    kbuf = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, buf, BUFFER_SIZE)) {
        kfree(kbuf);
        return -EFAULT;
    }

    // Set column and page addresses for full update (horizontal addressing assumed)
    {
        u8 set_addr_cmds[] = {
            0x21, 0x00, 0x7F, // column address: 0..127
            0x22, 0x00, 0x07  // page address: 0..7  (64 rows /8)
        };
        ret = ssd1306_send_command(ssd1306_client, set_addr_cmds, sizeof(set_addr_cmds));
        if (ret < 0) {
            kfree(kbuf);
            return ret;
        }
    }

    ret = ssd1306_send_data(ssd1306_client, kbuf, BUFFER_SIZE);
    kfree(kbuf);
    if (ret < 0) return ret;

    return count;
}

static const struct file_operations ssd1306_fops = {
    .owner = THIS_MODULE,
    .write = ssd1306_write,
};

static int ssd1306_probe(struct i2c_client *client)
{
    int ret;

    ssd1306_client = client;

    dev_info(&client->dev, DRIVER_NAME ": probing at 0x%02x\n", client->addr);

    ret = ssd1306_init_display(client);
    if (ret < 0) {
        dev_err(&client->dev, "init failed: %d\n", ret);
        return ret;
    }

    // create device node
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret) {
        dev_err(&client->dev, "alloc_chrdev_region failed\n");
        return ret;
    }

    cdev_init(&ssd1306_cdev, &ssd1306_fops);
    ssd1306_cdev.owner = THIS_MODULE;
    ret = cdev_add(&ssd1306_cdev, dev_number, 1);
    if (ret) {
        unregister_chrdev_region(dev_number, 1);
        return ret;
    }

    ssd1306_class = class_create(DEVICE_NAME);
    if (IS_ERR(ssd1306_class)) {
        cdev_del(&ssd1306_cdev);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(ssd1306_class);
    }

    if (!device_create(ssd1306_class, NULL, dev_number, NULL, DEVICE_NAME)) {
        class_destroy(ssd1306_class);
        cdev_del(&ssd1306_cdev);
        unregister_chrdev_region(dev_number, 1);
        return -ENOMEM;
    }

    dev_info(&client->dev, DRIVER_NAME ": device /dev/%s created\n", DEVICE_NAME);
    return 0;
}

static void ssd1306_remove(struct i2c_client *client)
{
    device_destroy(ssd1306_class, dev_number);
    class_destroy(ssd1306_class);
    cdev_del(&ssd1306_cdev);
    unregister_chrdev_region(dev_number, 1);

    dev_info(&client->dev, DRIVER_NAME ": removed\n");
}

static const struct i2c_device_id ssd1306_id[] = {
    { "ssd1306", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ssd1306_id);
static const struct of_device_id ssd1306_of_match[] = {
    { .compatible = "solomon,ssd1306" },
    { }
};
MODULE_DEVICE_TABLE(of, ssd1306_of_match);

static struct i2c_driver ssd1306_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = ssd1306_of_match,
    },
    .probe = ssd1306_probe,
    .remove = ssd1306_remove,
    .id_table = ssd1306_id,

};

module_i2c_driver(ssd1306_driver);

MODULE_AUTHOR("ChatGPT (example)");
MODULE_DESCRIPTION("Simple SSD1306 I2C driver exposing /dev/ssd1306 (write 1024 bytes)");
MODULE_LICENSE("GPL");
