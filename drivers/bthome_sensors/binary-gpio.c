#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/pm/device_runtime.h>

#include <lib/bthome.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT bthome_binary_gpio

LOG_MODULE_REGISTER(bthome_binary_gpio, CONFIG_BTHOME_SENSORS_LOG_LEVEL);

struct bthome_binary_gpio_data {
	uint8_t value;
	bool event;
	struct gpio_callback callback;
};

struct bthome_binary_gpio_config {
	struct gpio_dt_spec input;
};

static void binary_sensor_triggered(const struct device *dev, struct gpio_callback *cb,
				    uint32_t pins)
{
	struct bthome_binary_gpio_data *data =
		CONTAINER_OF(cb, struct bthome_binary_gpio_data, callback);
	gpio_port_value_t value;
	int err;

	/* since we get a pinmask and not a single pin, we can't use the gpio_pin_get function here.
	 */
	err = gpio_port_get(dev, &value);
	if (err) {
		LOG_ERR("Could not read GPIO value.");
		return;
	}
	if (value & pins) {
		data->value = 1;
	} else {
		data->value = 0;
	}

	/* notify bthome, that data has changed */
	bthome_data_changed();
}

static int bthome_binary_gpio_init(const struct device *dev)
{
	const struct bthome_binary_gpio_config *cfg = dev->config;
	struct bthome_binary_gpio_data *data = dev->data;
	int err;

	if (!gpio_is_ready_dt(&cfg->input)) {
		LOG_ERR("Error: Sensor device %s is not ready", cfg->input.port->name);
		return -EBADFD;
	}

	err = gpio_pin_configure_dt(&cfg->input, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", err, cfg->input.port->name,
			cfg->input.pin);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&cfg->input, GPIO_INT_EDGE_BOTH);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", err,
			cfg->input.port->name, cfg->input.pin);
		return err;
	}

	gpio_init_callback(&data->callback, binary_sensor_triggered, BIT(cfg->input.pin));

	err = gpio_add_callback(cfg->input.port, &data->callback);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure callback on %s pin %d", err,
			cfg->input.port->name, cfg->input.pin);
		return err;
	}

	LOG_DBG("Set up gpio at %s pin %d", cfg->input.port->name, cfg->input.pin);

	data->value = gpio_pin_get_dt(&cfg->input);

	LOG_DBG("Read value %d from %s -> %d", data->value, cfg->input.port->name, cfg->input.pin);
	LOG_DBG("Value: %d", data->value);

	return 0;
}

#define BTHOME_BINARY_GPIO_DEFINE(inst)                                                            \
	static struct bthome_binary_gpio_data bthome_binary_gpio_driver_##inst = {                 \
		.event = false,                                                                    \
	};                                                                                         \
                                                                                                   \
	BTHOME_DEFINE_SENSOR(bthome_binary_gpio_##inst, DT_INST_PROP(inst, bthome_sensor_type),    \
			     &bthome_binary_gpio_driver_##inst.value, 1,                           \
			     &bthome_binary_gpio_driver_##inst.event);                             \
                                                                                                   \
	static const struct bthome_binary_gpio_config bthome_binary_gpio_cfg_##inst = {            \
		.input = GPIO_DT_SPEC_INST_GET(inst, input_gpios),                                 \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, bthome_binary_gpio_init, NULL,                                 \
			      &bthome_binary_gpio_driver_##inst, &bthome_binary_gpio_cfg_##inst,   \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(BTHOME_BINARY_GPIO_DEFINE)
