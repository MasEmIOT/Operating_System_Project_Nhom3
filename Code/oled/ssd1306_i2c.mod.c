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
	{ 0xb90015ae, "i2c_register_driver" },
	{ 0x8b970f46, "device_destroy" },
	{ 0x6775d5d3, "class_destroy" },
	{ 0x27271c6b, "cdev_del" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x3bb3b979, "_dev_info" },
	{ 0x4829a47e, "memcpy" },
	{ 0xbe64227a, "i2c_transfer_buffer_flags" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xbbd8f39f, "_dev_err" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xa01f13a6, "cdev_init" },
	{ 0x3a6d85d3, "cdev_add" },
	{ 0x59c02473, "class_create" },
	{ 0x2c9a4c10, "device_create" },
	{ 0xcb8314a8, "i2c_del_driver" },
	{ 0x4fc61f86, "kmalloc_caches" },
	{ 0x5443de3e, "__kmalloc_cache_noprof" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x37a0cba, "kfree" },
	{ 0xdcb764ad, "memset" },
	{ 0x92997ed8, "_printk" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Csolomon,ssd1306");
MODULE_ALIAS("of:N*T*Csolomon,ssd1306C*");
MODULE_ALIAS("i2c:ssd1306");

MODULE_INFO(srcversion, "7EB5D53A9789BB983028C7D");
