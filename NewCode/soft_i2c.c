#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>

/* CHẾ ĐỘ GIẢ LẬP CHO PC la 1 - real =0*/
#define SIMULATION_MODE 1 

#define SDA_PIN 2
#define SCL_PIN 3

MODULE_LICENSE("GPL");
MODULE_AUTHOR("User");

/* MOCK GPIO */
#if SIMULATION_MODE
    static void gpio_set_value(int pin, int val) {}
    static int gpio_get_value(int pin) { return 0; }
    static void gpio_direction_output(int pin, int val) {}
    static void gpio_direction_input(int pin) {}
    static int gpio_request(unsigned gpio, const char *label) { return 0; }
    static void gpio_free(unsigned gpio) {}
    #define udelay(x) 
#else
    #include <linux/gpio.h>
#endif

void my_i2c_start(void) {
    gpio_direction_output(SDA_PIN, 1);
    gpio_set_value(SDA_PIN, 1);
    gpio_set_value(SCL_PIN, 1);
    udelay(5);
    gpio_set_value(SDA_PIN, 0);
    udelay(5);
    gpio_set_value(SCL_PIN, 0);
}
EXPORT_SYMBOL(my_i2c_start);

void my_i2c_stop(void) {
    gpio_direction_output(SDA_PIN, 1);
    gpio_set_value(SDA_PIN, 0);
    gpio_set_value(SCL_PIN, 1);
    udelay(5);
    gpio_set_value(SDA_PIN, 1);
    udelay(5);
}
EXPORT_SYMBOL(my_i2c_stop);

int my_i2c_wait_ack(void) {
    return 0; // Giả lập luôn ACK
}
EXPORT_SYMBOL(my_i2c_wait_ack);

void my_i2c_write_byte(unsigned char byte) {}
EXPORT_SYMBOL(my_i2c_write_byte);

unsigned char my_i2c_read_byte(unsigned char ack) {
#if SIMULATION_MODE
    static int counter = 0;
    counter++;
    return (counter * 10) & 0xFF; // Trả về dữ liệu giả thay đổi
#else
    return 0;
#endif
}
EXPORT_SYMBOL(my_i2c_read_byte);

static int __init my_i2c_init(void) { return 0; }
static void __exit my_i2c_exit(void) { }

module_init(my_i2c_init);
module_exit(my_i2c_exit);
