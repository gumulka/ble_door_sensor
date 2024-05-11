#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#include "bthome.h"
#include "environment-sensors.h"

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

static const struct gpio_dt_spec hall_sensor = GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios);
static struct gpio_callback hall_sensor_callback;
static struct gpio_callback button_callback;

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
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

	LOG_INF("Bluetooth initialized");

	/* Start advertising */
	err = bt_le_adv_start(
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY,
				BT_LE_ADV_INTERVAL_MAX / 2, BT_LE_ADV_INTERVAL_MAX, NULL),
		ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
}

static void ble_adv_handler(struct k_work *_work)
{
	LOG_INF("Updating BLE ADV Data");
	int ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
	LOG_HEXDUMP_DBG(service_data, ARRAY_SIZE(service_data), "Service data:");
	if (ret) {
		LOG_ERR("Failed to update advertising data (err %d)", ret);
	}
}
K_WORK_DEFINE(ble_adv_work, ble_adv_handler);

static void read_sensors_cb(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_MINUTES(10));

	env_read_sensor_data(service_data + POS_BATTERY_DATA, service_data + POS_TEMPERATURE_DATA,
			     service_data + POS_HUMIDITY_DATA, service_data + POS_ILLUMINANCE_DATA);

	k_work_submit(&ble_adv_work);
	return;
}
K_WORK_DELAYABLE_DEFINE(read_sensors_work, read_sensors_cb);

static void read_sensor_data()
{
	service_data[POS_HALL_EFFECT_DATA] = !gpio_pin_get_dt(&hall_sensor);
	service_data[POS_BUTTON_DATA] = !!gpio_pin_get_dt(&button);
	LOG_DBG("Sensor values: %d, %d", service_data[POS_HALL_EFFECT_DATA],
		service_data[POS_BUTTON_DATA]);
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	read_sensor_data();
	k_work_submit(&ble_adv_work);
}

static int configure_sensor(const struct gpio_dt_spec *hall_sensor,
			    struct gpio_callback *hall_sensor_callback)
{
	int ret;
	if (!gpio_is_ready_dt(hall_sensor)) {
		LOG_ERR("Error: Sensor device %s is not ready", hall_sensor->port->name);
		return -EBADFD;
	}

	ret = gpio_pin_configure_dt(hall_sensor, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", ret, hall_sensor->port->name,
			hall_sensor->pin);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(hall_sensor, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret,
			hall_sensor->port->name, hall_sensor->pin);
		return ret;
	}

	gpio_init_callback(hall_sensor_callback, button_pressed, BIT(hall_sensor->pin));
	gpio_add_callback(hall_sensor->port, hall_sensor_callback);
	LOG_DBG("Set up button at %s pin %d", hall_sensor->port->name, hall_sensor->pin);
	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("BLE Door Sensor");

	ret = configure_sensor(&hall_sensor, &hall_sensor_callback);
	if (ret < 0) {
		return ret;
	}

	ret = configure_sensor(&button, &button_callback);
	if (ret < 0) {
		return ret;
	}

	env_init_sensors();

	read_sensor_data();

	env_read_sensor_data(service_data + POS_BATTERY_DATA, service_data + POS_TEMPERATURE_DATA,
			     service_data + POS_HUMIDITY_DATA, service_data + POS_ILLUMINANCE_DATA);

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(bt_ready);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	k_work_schedule(&read_sensors_work, K_MINUTES(1));

	return 0;
}
