#include "environment-sensors.h"
#include "bthome.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(environment_sensors, CONFIG_APP_LOG_LEVEL);

static const struct adc_dt_spec soc_voltage = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

static const struct device *const shtc = DEVICE_DT_GET(DT_ALIAS(ambient_temp0));
static const struct device *const light = DEVICE_DT_GET(DT_ALIAS(ambient_light0));

static void read_shtc(uint8_t *temperature, uint8_t *humidity)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch_chan(shtc, SENSOR_CHAN_ALL);
	if (ret < 0) {
		LOG_WRN("Could not fetch shtc data: %d", ret);
		return;
	}

	ret = sensor_channel_get(shtc, SENSOR_CHAN_AMBIENT_TEMP, &value);
	if (ret < 0) {
		LOG_WRN("Could not get temperature: %d", ret);
		return;
	}
	LOG_DBG("Temperature is %d°C", value.val1);
	uint16_t temp = (uint16_t)(value.val1 * 100 + value.val2 / 10000);
	temperature[1] = (temp >> 8) & 0xFF;
	temperature[0] = temp & 0xFF;

	ret = sensor_channel_get(shtc, SENSOR_CHAN_HUMIDITY, &value);
	if (ret < 0) {
		LOG_WRN("Could not get humidity: %d", ret);
		return;
	}
	LOG_DBG("Humidity is %d percent", value.val1);
	*humidity = (uint8_t)(value.val1 & 0xFF);
}

static void read_ambient_light(uint8_t *illuminance)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch_chan(light, SENSOR_CHAN_ALL);
	if (ret < 0) {
		LOG_WRN("Could not fetch ambient light data: %d", ret);
		return;
	}

	ret = sensor_channel_get(light, SENSOR_CHAN_LIGHT, &value);
	if (ret < 0) {
		LOG_WRN("Could not get ambient light: %d", ret);
		return;
	}
	uint32_t temp = (uint32_t)(value.val1 * 100 + value.val2 / 10000);
	LOG_DBG("Light is %d centi lux", temp);
	illuminance[0] = temp & 0xFF;
	illuminance[1] = (temp >> 8) & 0xFF;
	illuminance[2] = (temp >> 16) & 0xFF;
}

static void read_supply_voltage(uint8_t *battery)
{
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
			*battery = BTHOME_VALUE_BATTERY_ERROR;
		}
		return;
	}
	adc_read_error_counter = 0;
	int32_t batt = (int32_t)buf;
	LOG_DBG("Raw value: %" PRId32, batt);
	adc_raw_to_millivolts_dt(&soc_voltage, &batt);
	// convert mv to percentage with 3.3V beeing 100% and 2.5V beeing 0%
	// This is not a battery curve, just some calculations for better or worse.
	batt -= 2500;
	batt /= 8;
	if (batt > 100) {
		batt = 100;
	} else if (batt < 0) {
		batt = 0;
	}
	LOG_INF("New batt value: %d", batt);
	*battery = (uint8_t)batt;
}

int env_init_sensors()
{
	if (!adc_is_ready_dt(&soc_voltage)) {
		printk("ADC controller device %s not ready\n", soc_voltage.dev->name);
		return -EBADFD;
	}

	int ret = adc_channel_setup_dt(&soc_voltage);
	if (ret < 0) {
		printk("Could not setup adc (%d)\n", ret);
		return ret;
	}

	return 0;
}

int env_read_sensor_data(uint8_t *battery, uint8_t *temperature, uint8_t *humidity,
			 uint8_t *illuminance)
{
	int ret = 0;
	read_shtc(temperature, humidity);
	read_ambient_light(illuminance);
	read_supply_voltage(battery);
	return ret;
}
