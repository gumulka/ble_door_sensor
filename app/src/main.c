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
#include "interrupt-sensors.h"

static uint8_t service_data[] = {
	BT_UUID_16_ENCODE(BTHOME_SERVICE_UUID),
	BTHOME_INFO_VERSION | BTHOME_INFO_IRREGULAR_INTERVAL | BTHOME_INFO_UNENCRYPTED_DATA,
	BTHOME_SENSOR_BINARY_WINDOW,
	BTHOME_VALUE_DOOR_CLOSED,
	BTHOME_SENSOR_BUTTON,
	BTHOME_VALUE_BUTTON_NONE,
	BTHOME_SENSOR_BATTERY,
	0,
	BTHOME_SENSOR_TEMPERATURE,
	0,
	0,
	BTHOME_SENSOR_HUMIDITY_8,
	0,
	BTHOME_SENSOR_ILLUMINANCE,
	0,
	0,
	0,
};

#define POS_HALL_EFFECT_DATA 4
#define POS_BUTTON_DATA      6
#define POS_BATTERY_DATA     8
#define POS_TEMPERATURE_DATA 10
#define POS_HUMIDITY_DATA    13
#define POS_ILLUMINANCE_DATA 15

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data))};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

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
	err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
					      BT_LE_ADV_INTERVAL_MAX / 2, BT_LE_ADV_INTERVAL_MAX,
					      NULL),
			      ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
}

static void read_sensors_cb(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_SECONDS(30));

	gpio_pin_set_dt(&led, 1);
	env_read_sensor_data(service_data + POS_BATTERY_DATA, service_data + POS_TEMPERATURE_DATA,
			     service_data + POS_HUMIDITY_DATA, service_data + POS_ILLUMINANCE_DATA);

	int_read_sensor_data(service_data + POS_BUTTON_DATA, service_data + POS_HALL_EFFECT_DATA);

	LOG_INF("Updating BLE ADV Data");
	int ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
	LOG_HEXDUMP_DBG(service_data, ARRAY_SIZE(service_data), "Service data:");
	if (ret) {
		LOG_ERR("Failed to update advertising data (err %d)", ret);
	}
	gpio_pin_set_dt(&led, 0);
}
K_WORK_DELAYABLE_DEFINE(read_sensors_work, read_sensors_cb);

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

	env_init_sensors();
	int_init_sensors(&read_sensors_work);

	env_read_sensor_data(service_data + POS_BATTERY_DATA, service_data + POS_TEMPERATURE_DATA,
			     service_data + POS_HUMIDITY_DATA, service_data + POS_ILLUMINANCE_DATA);
	int_read_sensor_data(service_data + POS_BUTTON_DATA, service_data + POS_HALL_EFFECT_DATA);

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(bt_ready);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	k_work_schedule(&read_sensors_work, K_SECONDS(5));

	// short blink to signal everything is okay
	for(int i = 0; i <3; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(80);
		gpio_pin_set_dt(&led, 0);
		k_msleep(100);
	}

	return 0;
}
