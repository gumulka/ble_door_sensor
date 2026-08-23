#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#include "bthome.h"
#include "environment-sensors.h"
#include "blink.h"

static uint8_t service_data[] = {
	BT_UUID_16_ENCODE(BTHOME_SERVICE_UUID),
	BTHOME_INFO_VERSION | BTHOME_INFO_IRREGULAR_INTERVAL | BTHOME_INFO_UNENCRYPTED_DATA,
	BTHOME_SENSOR_BATTERY,
	0,
	BTHOME_SENSOR_ENERGY_WH,
	0,
	0,
	0,
	0,
};

#define POS_BATTERY_DATA     4
#define POS_ENERGY_WH_DATA   6

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data))};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *const light = DEVICE_DT_GET(DT_ALIAS(ambient_light0));

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Rebooting system");

	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	/* Start advertising */
	err = bt_le_adv_start(
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
				BT_LE_ADV_INTERVAL_MAX / 2, BT_LE_ADV_INTERVAL_MAX, NULL),
		ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
}

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("No LED defined");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Could not configure LED.");
		return 0;
	}

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(bt_ready);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	battery_init(ad, ARRAY_SIZE(ad), service_data + POS_BATTERY_DATA);
	energy_init(light, ad, ARRAY_SIZE(ad), (uint32_t*) (service_data + POS_ENERGY_WH_DATA));

	// short blink to signal everything is okay
	for(int i = 0; i <3; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(80);
		gpio_pin_set_dt(&led, 0);
		k_msleep(100);
	}

	return 0;
}
