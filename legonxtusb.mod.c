#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
};

static const struct modversion_info ____versions[]
__attribute_used__
__attribute__((section("__versions"))) = {
	{ 0xcb75e004, "usb_bulk_msg" },
	{ 0xf85bd710, "usb_register_dev" },
	{ 0xedd8bcce, "usb_get_dev" },
	{ 0x6491cc9c, "usb_find_interface" },
	{ 0xe0b7c175, "usb_register_driver" },
	{ 0xfa6a61fe, "usb_put_dev" },
	{ 0x833a467c, "usb_free_urb" },
	{ 0x225bf6fb, "usb_buffer_free" },
	{ 0xd2443b32, "usb_submit_urb" },
	{ 0x2e61f689, "usb_buffer_alloc" },
	{ 0x93cda85, "usb_alloc_urb" },
	{ 0xc560c341, "usb_deregister_dev" },
	{ 0x32d7b675, "usb_deregister" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=usbcore";

MODULE_ALIAS("usb:v0694p0002d*dc*dsc*dp*ic*isc*ip*");

MODULE_INFO(srcversion, "F5043A18876FCCB32D13ADC");
