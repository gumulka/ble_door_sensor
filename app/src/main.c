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

#define HAS_WINDOW_SENSOR        DT_NODE_EXISTS(DT_NODELABEL(hall_sensor))
#define HAS_BUTTON_SENSOR        DT_NODE_EXISTS(DT_NODELABEL(button))
#define HAS_BAT_VOLTAGE_SENSOR   DT_NODE_EXISTS(DT_NODELABEL(bat_voltage))
#define HAS_TEMP_HUMIDITY_SENSOR DT_HAS_ALIAS(ambient_temp0)
#define HAS_LIGHT_SENSOR         DT_HAS_ALIAS(ambient_light0)

static uint8_t service_data[] = {
	BT_UUID_16_ENCODE(BTHOME_SERVICE_UUID),
	BTHOME_INFO_VERSION | BTHOME_INFO_IRREGULAR_INTERVAL | BTHOME_INFO_UNENCRYPTED_DATA,
#if HAS_WINDOW_SENSOR
	BTHOME_SENSOR_BINARY_WINDOW,
	BTHOME_VALUE_DOOR_CLOSED,
#endif /* HAS_WINDOW_SENSOR */
#if HAS_BUTTON_SENSOR
	BTHOME_SENSOR_BUTTON,
	BTHOME_VALUE_BUTTON_NONE,
#endif /* HAS_BUTTON_SENSOR */
#if HAS_BAT_VOLTAGE_SENSOR
	BTHOME_SENSOR_BATTERY,
	0,
#endif /* HAS_BAT_VOLTAGE_SENSOR */
#if HAS_TEMP_HUMIDITY_SENSOR
	BTHOME_SENSOR_TEMPERATURE,
	0,
	0,
	BTHOME_SENSOR_HUMIDITY_8,
	0,
#endif /* HAS_TEMP_HUMIDITY_SENSOR */
#if HAS_LIGHT_SENSOR
	BTHOME_SENSOR_ILLUMINANCE,
	0,
#endif /* HAS_LIGHT_SENSOR */
	0,
	0,
};

#if HAS_TEMP_HUMIDITY_SENSOR
#define TEMP_SENSOR_LENGTH     3
#define HUMIDITY_SENSOR_LENGTH 2
#else
#define TEMP_SENSOR_LENGTH     0
#define HUMIDITY_SENSOR_LENGTH 0
#endif

#if HAS_LIGHT_SENSOR
#define LIGHT_SENSOR_LENGTH 2
#else
#define LIGHT_SENSOR_LENGTH 0
#endif

#if HAS_BAT_VOLTAGE_SENSOR
#define BAT_VOLTAGE_SENSOR_LENGTH 2
#else
#define BAT_VOLTAGE_SENSOR_LENGTH 0
#endif

#if HAS_BUTTON_SENSOR
#define BUTTON_SENSOR_LENGTH 2
#else
#define BUTTON_SENSOR_LENGTH 0
#endif

#if HAS_WINDOW_SENSOR
#define WINDOW_SENSOR_LENGTH 2
#else
#define WINDOW_SENSOR_LENGTH 0
#endif

#define PREFACE_DATA_LENGTH 3
#define DECLARATION_LENGTH  1

#define POS_HALL_EFFECT_DATA PREFACE_DATA_LENGTH + DECLARATION_LENGTH
#define POS_BUTTON_DATA      POS_HALL_EFFECT_DATA + WINDOW_SENSOR_LENGTH
#define POS_BATTERY_DATA     POS_BUTTON_DATA + BUTTON_SENSOR_LENGTH
#define POS_TEMPERATURE_DATA POS_BATTERY_DATA + BAT_VOLTAGE_SENSOR_LENGTH
#define POS_HUMIDITY_DATA    POS_TEMPERATURE_DATA + TEMP_SENSOR_LENGTH
#define POS_ILLUMINANCE_DATA POS_HUMIDITY_DATA + HUMIDITY_SENSOR_LENGTH

#if POS_ILLUMINANCE_DATA == 4
#error Must declare at least one sensor!
#endif

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data))};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#if HAS_BAT_VOLTAGE_SENSOR
static const struct adc_dt_spec soc_voltage = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
#else
#warning There is no battery voltage sensor!
#endif
#if HAS_TEMP_HUMIDITY_SENSOR
static const struct device *const shtc = DEVICE_DT_GET(DT_ALIAS(ambient_temp0));
#else
#warning There is no ambient temperature and humidity sensor!
#endif
#if HAS_LIGHT_SENSOR
static const struct device *const light = DEVICE_DT_GET(DT_ALIAS(ambient_light0));
#else
#warning There is no light sensor!
#endif

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Rebooting system");

	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}

int read_environment_sensors()
{
#if HAS_TEMP_HUMIDITY_SENSOR
	read_shtc(shtc, service_data + POS_TEMPERATURE_DATA, service_data + POS_HUMIDITY_DATA);
#endif /* HAS_TEMP_HUMIDITY_SENSOR */
#if HAS_BAT_VOLTAGE_SENSOR
	read_supply_voltage(&soc_voltage, service_data + POS_BATTERY_DATA);
#endif /* HAS_BAT_VOLTAGE_SENSOR */
#if HAS_LIGHT_SENSOR
	read_ambient_light(light, service_data + POS_ILLUMINANCE_DATA);
#endif /* HAS_LIGHT_SENSOR */
	return 0;
}

void update_bt_data()
{
	LOG_INF("Updating BLE ADV Data");
	int ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
	LOG_HEXDUMP_DBG(service_data, ARRAY_SIZE(service_data), "Service data:");
	if (ret) {
		LOG_ERR("Failed to update advertising data (err %d)", ret);
	}
}

static void read_sensors_cb(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_SECONDS(30));

	gpio_pin_set_dt(&led, 1);

	read_environment_sensors();

	int_read_sensor_data(service_data + POS_BUTTON_DATA, service_data + POS_HALL_EFFECT_DATA);

	update_bt_data();

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

#if HAS_BAT_VOLTAGE_SENSOR
	if (!adc_is_ready_dt(&soc_voltage)) {
		printk("ADC controller device %s not ready\n", soc_voltage.dev->name);
		return -EBADFD;
	}

	ret = adc_channel_setup_dt(&soc_voltage);
	if (ret < 0) {
		printk("Could not setup adc (%d)\n", ret);
		return ret;
	}
#endif /* HAS_BAT_VOLTAGE_SENSOR */

	int_init_sensors(&read_sensors_work);

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(NULL);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	int_read_sensor_data(service_data + POS_BUTTON_DATA, service_data + POS_HALL_EFFECT_DATA);
	read_environment_sensors();

	/* Start advertising */
	ret = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
					      BT_LE_ADV_INTERVAL_MAX / 2, BT_LE_ADV_INTERVAL_MAX,
					      NULL),
			      ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		LOG_ERR("Advertising failed to start (err %d)", ret);
		return ret;
	}

	k_work_schedule(&read_sensors_work, K_SECONDS(30));

	// short blink to signal everything is okay
	for(int i = 0; i <3; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(80);
		gpio_pin_set_dt(&led, 0);
		k_msleep(100);
	}

	return 0;
}
