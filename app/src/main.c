#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define BTHOME_INFO_VERSION            0x40
#define BTHOME_INFO_REGULAR_INTERVAL   0x00
#define BTHOME_INFO_IRREGULAR_INTERVAL 0x04
#define BTHOME_INFO_ENCRYPTED_DATA     0x01
#define BTHOME_INFO_UNENCRYPTED_DATA   0x00

#define BTHOME_SENSOR_BATTERY        0x01
#define BTHOME_SENSOR_BINARY_BATTERY 0x15
#define BTHOME_SENSOR_BINARY_DOOR    0x1A
#define BTHOME_SENSOR_BINARY_WINDOW  0x2D

#define BTHOME_VALUE_DOOR_CLOSED   0x00
#define BTHOME_VALUE_DOOR_OPEN     0x01
#define BTHOME_VALUE_WINDOW_CLOSED 0x00
#define BTHOME_VALUE_WINDOW_OPEN   0x01
#define BTHOME_VALUE_BATTERY_ERROR 0xFF // just a first guess from my side. Need to verify

#define BTHOME_SERVICE_UUID 0xfcd2

static uint8_t service_data[] = {
	BT_UUID_16_ENCODE(BTHOME_SERVICE_UUID),
	BTHOME_INFO_VERSION | BTHOME_INFO_IRREGULAR_INTERVAL | BTHOME_INFO_UNENCRYPTED_DATA,
	BTHOME_SENSOR_BINARY_WINDOW,
	BTHOME_VALUE_DOOR_CLOSED,
	BTHOME_SENSOR_BINARY_WINDOW,
	BTHOME_VALUE_DOOR_CLOSED,
	BTHOME_SENSOR_BATTERY,
	0,
};

#define POS_FIRST_WINDOW_DATA  4
#define POS_SECOND_WINDOW_DATA 6
#define POS_BATTERY_DATA       8

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data))};

static const struct gpio_dt_spec hall_sensor_left =
	GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor_left), gpios);
static const struct gpio_dt_spec hall_sensor_right =
	GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor_right), gpios);
static struct gpio_callback hall_sensor_left_callback;
static struct gpio_callback hall_sensor_right_callback;

static const struct adc_dt_spec soc_voltage = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	/* Start advertising */
	err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
					      BT_LE_ADV_INTERVAL_MAX / 2, BT_LE_ADV_INTERVAL_MAX,
					      NULL),
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

static void read_sensor_data()
{
	service_data[POS_FIRST_WINDOW_DATA] = !gpio_pin_get_dt(&hall_sensor_left);
	service_data[POS_SECOND_WINDOW_DATA] = !gpio_pin_get_dt(&hall_sensor_right);
	LOG_DBG("Sensor values: %d, %d", service_data[POS_FIRST_WINDOW_DATA],
		service_data[POS_SECOND_WINDOW_DATA]);
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	read_sensor_data();
	k_work_submit(&ble_adv_work);
}

static void read_supply_voltage(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_HOURS(12));
	LOG_DBG("Reading ADC");
	static int adc_read_error_counter = 0;
	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};
	(void)adc_sequence_init_dt(&soc_voltage, &sequence);

	int err = adc_read_dt(&soc_voltage, &sequence);
	if (err < 0) {
		printk("Could not read ADC (%d)\n", err);
		adc_read_error_counter++;
		if (adc_read_error_counter > 10) {
			service_data[POS_BATTERY_DATA] = BTHOME_VALUE_BATTERY_ERROR;
		}
		return;
	}
	adc_read_error_counter = 0;
	int32_t battery = (int32_t)buf;
	LOG_DBG("Raw value: %" PRId32, battery);
	adc_raw_to_millivolts_dt(&soc_voltage, &battery);
	// convert mv to percentage with 3.3V beeing 100% and 2.5V beeing 0%
	// This is not a battery curve, just some calculations for better or worse.
	battery -= 2500;
	battery /= 8;
	if (battery > 100) {
		battery = 100;
	} else if (battery < 0) {
		battery = 0;
	}
	LOG_INF("New Battery value: %d", battery);
	service_data[POS_BATTERY_DATA] = (char)battery;
	k_work_submit(&ble_adv_work);
}
K_WORK_DELAYABLE_DEFINE(adc_read_work, read_supply_voltage);

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

static int configure_adc(const struct adc_dt_spec *adc)
{
	if (!adc_is_ready_dt(adc)) {
		printk("ADC controller device %s not ready\n", adc->dev->name);
		return -EBADFD;
	}

	int ret = adc_channel_setup_dt(adc);
	if (ret < 0) {
		printk("Could not setup adc (%d)\n", ret);
		return ret;
	}

	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("BLE Door Sensor");

	ret = configure_sensor(&hall_sensor_left, &hall_sensor_left_callback);
	if (ret < 0) {
		return ret;
	}

	ret = configure_sensor(&hall_sensor_right, &hall_sensor_right_callback);
	if (ret < 0) {
		return ret;
	}

	ret = configure_adc(&soc_voltage);
	if (ret < 0) {
		return ret;
	}

	read_sensor_data();
	read_supply_voltage(&adc_read_work.work);

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(bt_ready);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	return 0;
}
