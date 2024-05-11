/*
 * Copyright (c) 2023 Google LLC
 * Copyright (c) 2024 Fabian Pflug
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Based upon the NTC-Thermistor code
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT phototransistor_special

LOG_MODULE_REGISTER(phototransistor, CONFIG_SENSOR_LOG_LEVEL);

struct phototransistor_data {
	struct k_mutex mutex;
	int32_t raw;
	int32_t sample_val;
};

struct phototransistor_config {
	const struct adc_dt_spec adc_channel;
	struct gpio_dt_spec enable;
	uint32_t pulldown_ohm;
};

static int phototransistor_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct phototransistor_data *data = dev->data;
	const struct phototransistor_config *cfg = dev->config;
	int32_t val_mv;
	int res;
	struct adc_sequence sequence = {
		.options = NULL,
		.buffer = &data->raw,
		.buffer_size = sizeof(data->raw),
		.calibrate = false,
	};

	k_mutex_lock(&data->mutex, K_FOREVER);

	int err = gpio_pin_set_dt(&cfg->enable, 1);
	if (err != 0) {
		LOG_ERR("Could not set enable pin!");
		k_mutex_unlock(&data->mutex);
		return -EIO;
	}

	// Wait for output voltage to stabilize
	k_sleep(K_USEC(150));

	adc_sequence_init_dt(&cfg->adc_channel, &sequence);
	res = adc_read(cfg->adc_channel.dev, &sequence);
	if (!res) {
		val_mv = data->raw;
		// If the number is 16 bit signed negative, then there is an error.
		// Especially since we have less then 15 bit resolution.
		if (data->raw & 0x8000) {
			data->sample_val = 0;
		} else {
			res = adc_raw_to_millivolts_dt(&cfg->adc_channel, &val_mv);
			data->sample_val = val_mv;
		}
		LOG_DBG("Measured: %d -> %d mV", data->raw, data->sample_val);
	}

	gpio_pin_set_dt(&cfg->enable, 0);

	k_mutex_unlock(&data->mutex);

	return res;
}

static int phototransistor_channel_get(const struct device *dev, enum sensor_channel chan,
				       struct sensor_value *val)
{
	struct phototransistor_data *data = dev->data;
	const struct phototransistor_config *cfg = dev->config;
	int32_t temp;

	switch (chan) {
	case SENSOR_CHAN_LIGHT:
		temp = data->sample_val;
		temp = 100 * temp * 6666 / cfg->pulldown_ohm;
		val->val1 = temp / 100;
		val->val2 = (temp % 100) * 10000;
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static const struct sensor_driver_api phototransistor_driver_api = {
	.sample_fetch = phototransistor_sample_fetch,
	.channel_get = phototransistor_channel_get,
};

static int phototransistor_init(const struct device *dev)
{
	const struct phototransistor_config *cfg = dev->config;
	int err;

	if (!gpio_is_ready_dt(&cfg->enable)) {
		LOG_ERR("GPIO port %s is not ready", cfg->enable.port->name);
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&cfg->enable, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		LOG_ERR("Could not configure enable pin!");
		return err;
	}

	if (!adc_is_ready_dt(&cfg->adc_channel)) {
		LOG_ERR("ADC controller device is not ready\n");
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&cfg->adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup channel err(%d)\n", err);
		return err;
	}

	return 0;
}

#define PHOTOTRANSISTOR_DEFINE(inst)                                                               \
	static struct phototransistor_data phototransistor_driver_##inst;                          \
                                                                                                   \
	static const struct phototransistor_config phototransistor_cfg_##inst = {                  \
		.adc_channel = ADC_DT_SPEC_INST_GET(inst),                                         \
		.enable = GPIO_DT_SPEC_INST_GET(inst, enable_gpios),                               \
		.pulldown_ohm = DT_INST_PROP(inst, pulldown_ohm),                                  \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, phototransistor_init, NULL,                             \
				     &phototransistor_driver_##inst, &phototransistor_cfg_##inst,  \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                     \
				     &phototransistor_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PHOTOTRANSISTOR_DEFINE)
