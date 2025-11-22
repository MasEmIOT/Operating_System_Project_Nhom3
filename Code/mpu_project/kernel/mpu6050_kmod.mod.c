#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xdcb764ad, "memset" },
	{ 0x92997ed8, "_printk" },
	{ 0x49d0b6b3, "i2c_get_adapter" },
	{ 0x476b165a, "sized_strscpy" },
	{ 0x7c0d9ba2, "i2c_new_client_device" },
	{ 0x7bbd7f8a, "i2c_put_adapter" },
	{ 0xb90015ae, "i2c_register_driver" },
	{ 0xb063ac3a, "i2c_unregister_device" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x8b970f46, "device_destroy" },
	{ 0x6775d5d3, "class_destroy" },
	{ 0x27271c6b, "cdev_del" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x95d03246, "device_remove_file" },
	{ 0x81718f5f, "i2c_smbus_read_byte_data" },
	{ 0xbbd8f39f, "_dev_err" },
	{ 0x3bb3b979, "_dev_info" },
	{ 0x8dbfeb5a, "i2c_smbus_write_byte_data" },
	{ 0xf9a482f9, "msleep" },
	{ 0x91b8d433, "device_create_file" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xa01f13a6, "cdev_init" },
	{ 0x3a6d85d3, "cdev_add" },
	{ 0x59c02473, "class_create" },
	{ 0x2c9a4c10, "device_create" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xcb8314a8, "i2c_del_driver" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0xfe548aae, "param_ops_int" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("i2c:mpu6050");

MODULE_INFO(srcversion, "758338D2699A16531D9765D");
