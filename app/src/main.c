#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Rebooting system");

	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}

int main(void)
{
	printk("Starting BLE Door Sensor\n");

	// short blink to signal everything is okay
	for(int i = 0; i <3; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(80);
		gpio_pin_set_dt(&led, 0);
		k_msleep(100);
	}

	return 0;
}
