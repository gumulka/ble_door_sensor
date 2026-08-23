#include "environment-sensors.h"
#include "bthome.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/bluetooth/bluetooth.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(environment_sensors, CONFIG_APP_LOG_LEVEL);

static const struct adc_dt_spec soc_voltage = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

static struct battery_reading {
	struct bt_data *data;
	size_t data_size;
	uint8_t *battery;
	struct k_work_delayable work;
} reading_g;

static void read_supply_voltage(uint8_t *battery)
{
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
			*battery = BTHOME_VALUE_BATTERY_ERROR;
		}
		return;
	}
	adc_read_error_counter = 0;
	int32_t batt = (int32_t)buf;
	adc_raw_to_millivolts_dt(&soc_voltage, &batt);
	// convert mv to percentage with 3V beeing 100% and 2V beeing 0%
	// This is not a battery curve, just some calculations for better or worse.
	batt -= 2000;
	batt /= 10;
	if (batt > 100) {
		batt = 100;
	} else if (batt < 0) {
		batt = 0;
	}
	LOG_DBG("Battery is at %d percent", batt);
	*battery = (uint8_t)batt;
}

static void read_sensors_cb(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_MINUTES(10));
	struct battery_reading *reading = CONTAINER_OF(work, struct battery_reading, work);

	uint8_t battery;
	read_supply_voltage(&battery);

	if (battery == *reading->battery) {
		return;
	}

	LOG_INF("Battery changed from %d to %d", *reading->battery, battery);
	*reading->battery = battery;

	int ret = bt_le_adv_update_data(reading->data, reading->data_size, NULL, 0);
	if (ret) {
		LOG_ERR("Failed to update advertising data (err %d)", ret);
	}
}

int battery_init(struct bt_data *data, size_t data_size, uint8_t *battery)
{
	reading_g.data = data;
	reading_g.data_size = data_size;
	reading_g.battery = battery;
	reading_g.work.work.handler = read_sensors_cb;
	reading_g.work.work.flags = K_WORK_DELAYABLE;

	if (!adc_is_ready_dt(&soc_voltage)) {
		printk("ADC controller device %s not ready\n", soc_voltage.dev->name);
		return -EBADFD;
	}

	int ret = adc_channel_setup_dt(&soc_voltage);
	if (ret < 0) {
		printk("Could not setup adc (%d)\n", ret);
		return ret;
	}

	k_work_schedule(&reading_g.work, K_MINUTES(10));

	return 0;
}