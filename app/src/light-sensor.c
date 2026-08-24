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

#include <zephyr/bluetooth/bluetooth.h>

#include "blink.h"

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT phototransistor_special

LOG_MODULE_REGISTER(phototransistor, CONFIG_SENSOR_LOG_LEVEL);

struct phototransistor_data {
	struct bt_data *data;
	size_t data_size;
	uint32_t *energy_wh;
	uint32_t last_reading;
	struct k_work_delayable work;
	const struct device *dev;
	int num_below_threshold;
};

struct phototransistor_config {
	const struct adc_dt_spec adc_channel;
	struct gpio_dt_spec enable;
	uint32_t pulldown_ohm;
};

static int phototransistor_sample_fetch(const struct device *dev, uint32_t *val_mv)
{
	const struct phototransistor_config *cfg = dev->config;
	uint32_t raw_val;
	int res;
	struct adc_sequence sequence = {
		.options = NULL,
		.buffer = &raw_val,
		.buffer_size = sizeof(raw_val),
		.calibrate = false,
	};

	// int err = gpio_pin_set_dt(&cfg->enable, 1);
	// if (err != 0) {
	// 	LOG_ERR("Could not set enable pin!");
	// 	return -EIO;
	// }

	// Wait for output voltage to stabilize
	// k_sleep(K_USEC(150));

	adc_sequence_init_dt(&cfg->adc_channel, &sequence);
	res = adc_read(cfg->adc_channel.dev, &sequence);
	if (!res) {
		*val_mv = raw_val;
		// If the number is 16 bit signed negative, then there is an error.
		// Especially since we have less then 15 bit resolution.
		if (raw_val & 0x8000) {
			*val_mv = 0;
		} else {
			res = adc_raw_to_millivolts_dt(&cfg->adc_channel, val_mv);
		}
		LOG_DBG("Measured: %d -> %d mV", raw_val, *val_mv);
	}

	// gpio_pin_set_dt(&cfg->enable, 0);

	return res;
}

static void read_sensors_cb(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_MSEC(30));
	struct phototransistor_data *data = CONTAINER_OF(work, struct phototransistor_data, work);

	uint32_t raw_mv = 0;
	int ret = phototransistor_sample_fetch(data->dev, &raw_mv);
	if (ret) {
		LOG_ERR("Could not read phototransistor (%d)", ret);
		return;
	}

	// It roughly toggles between 0 and 100mV, but I have seen 80 to 130 mV.
	if (raw_mv < 40) {
		data->num_below_threshold++;
	} else {
		data->num_below_threshold = 0;
	}

	if (data->num_below_threshold != 2) {
		return;
	}

	(*data->energy_wh)++;

	LOG_INF("Energy is now %d Wh", *data->energy_wh);

	ret = bt_le_adv_update_data(data->data, data->data_size, NULL, 0);
	if (ret) {
		LOG_ERR("Failed to update advertising data (err %d)", ret);
	}
}

int energy_init(const struct device *dev, struct bt_data *bt_data, size_t data_size,
		uint32_t *energy_wh)
{

	struct phototransistor_data *data = dev->data;
	data->data = bt_data;
	data->data_size = data_size;
	data->energy_wh = energy_wh;

	k_work_reschedule(&data->work, K_MSEC(30));
	return 0;
}

static int phototransistor_init(const struct device *dev)
{
	const struct phototransistor_config *cfg = dev->config;
	int err;

	if (!gpio_is_ready_dt(&cfg->enable)) {
		LOG_ERR("GPIO port %s is not ready", cfg->enable.port->name);
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&cfg->enable, GPIO_OUTPUT_ACTIVE);
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

	struct phototransistor_data *data = dev->data;
	data->dev = dev;

	return 0;
}

#define PHOTOTRANSISTOR_DEFINE(inst)                                                               \
	static struct phototransistor_data phototransistor_driver_##inst = {                       \
		.work = Z_WORK_DELAYABLE_INITIALIZER(read_sensors_cb),                             \
		.num_below_threshold = 0,                                                          \
	};                                                                                         \
                                                                                                   \
	static const struct phototransistor_config phototransistor_cfg_##inst = {                  \
		.adc_channel = ADC_DT_SPEC_INST_GET(inst),                                         \
		.enable = GPIO_DT_SPEC_INST_GET(inst, enable_gpios),                               \
		.pulldown_ohm = DT_INST_PROP(inst, pulldown_ohm),                                  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, phototransistor_init, NULL, &phototransistor_driver_##inst,    \
			      &phototransistor_cfg_##inst, POST_KERNEL,                            \
			      CONFIG_SENSOR_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PHOTOTRANSISTOR_DEFINE)
