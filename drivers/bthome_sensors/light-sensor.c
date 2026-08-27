#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include <lib/bthome.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT phototransistor_special

LOG_MODULE_REGISTER(phototransistor, CONFIG_BTHOME_SENSORS_LOG_LEVEL);

struct phototransistor_data {
	int8_t illuminance[3];
	bool event;
	struct k_work_delayable work;
	const struct device *dev;
};

struct phototransistor_config {
	const struct adc_dt_spec adc_channel;
	struct gpio_dt_spec enable;
	uint32_t pulldown_ohm;
};

static void light_sensor_update(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_SECONDS(CONFIG_BTHOME_LIGHT_SENSOR_UPDATE_INTERVAL));
	struct phototransistor_data *data = CONTAINER_OF(work, struct phototransistor_data, work);
	const struct phototransistor_config *cfg = data->dev->config;

	int32_t raw = 0, val_mv, lux;
	int err;

	struct adc_sequence sequence = {
		.options = NULL,
		.buffer = &raw,
		.buffer_size = sizeof(raw),
		.calibrate = false,
	};

	err = gpio_pin_set_dt(&cfg->enable, 1);
	if (err) {
		LOG_ERR("Could not set enable pin!");
		return;
	}

	err = adc_sequence_init_dt(&cfg->adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not setup channel err(%d)\n", err);
		return;
	}

	// Wait for output voltage to stabilize
	k_sleep(K_USEC(150));

	err = adc_read_dt(&cfg->adc_channel, &sequence);

	gpio_pin_set_dt(&cfg->enable, 0);

	if (err) {
		LOG_ERR("ADC read error: %d", err);
		return;
	}

	val_mv = raw;
	// If the number is 16 bit signed negative, then there is an error.
	// Especially since we have less then 15 bit resolution.
	if (raw & 0x8000) {
		LOG_ERR("ADC read error, raw value: %d", raw);
		return;
	}
	err = adc_raw_to_millivolts_dt(&cfg->adc_channel, &val_mv);
	if (err) {
		LOG_ERR("ADC conversion error: %d", err);
		return;
	}
	LOG_DBG("Measured: %d -> %d mV", raw, val_mv);

	lux = val_mv;
	lux = 100 * lux * 6666 / cfg->pulldown_ohm;

	LOG_DBG("Light is %d centi lux", lux);
	data->illuminance[0] = lux & 0xFF;
	data->illuminance[1] = (lux >> 8) & 0xFF;
	data->illuminance[2] = (lux >> 16) & 0xFF;

	/* notify bthome, that data has changed */
	bthome_data_changed();
}

static int phototransistor_init(const struct device *dev)
{
	const struct phototransistor_config *cfg = dev->config;
	struct phototransistor_data *data = dev->data;
	int err;

	data->dev = dev;

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

	light_sensor_update(&data->work.work);

	return 0;
}

#define PHOTOTRANSISTOR_DEFINE(inst)                                                               \
	static struct phototransistor_data phototransistor_driver_##inst = {                       \
		.illuminance = {0, 0, 0},                                                          \
		.event = false,                                                                    \
		.work = Z_WORK_DELAYABLE_INITIALIZER(light_sensor_update),                         \
	};                                                                                         \
                                                                                                   \
	BTHOME_DEFINE_SENSOR(phototransistor_##inst, BTHOME_SENSOR_ILLUMINANCE,                    \
			     phototransistor_driver_##inst.illuminance, 3,                         \
			     &phototransistor_driver_##inst.event);                                \
                                                                                                   \
	static const struct phototransistor_config phototransistor_cfg_##inst = {                  \
		.adc_channel = ADC_DT_SPEC_INST_GET(inst),                                         \
		.enable = GPIO_DT_SPEC_INST_GET(inst, enable_gpios),                               \
		.pulldown_ohm = DT_INST_PROP(inst, pulldown_ohm),                                  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, phototransistor_init, NULL, &phototransistor_driver_##inst,    \
			      &phototransistor_cfg_##inst, POST_KERNEL,                            \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(PHOTOTRANSISTOR_DEFINE)
