#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <lib/bthome.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT bthome_button

LOG_MODULE_REGISTER(bthome_button, CONFIG_BTHOME_SENSORS_LOG_LEVEL);

struct bthome_button_data {
	uint8_t value;
	bool event;
	uint8_t curr_value;
	struct gpio_callback callback;
	struct k_work_delayable publish_work;
	int64_t push_time;
};

struct bthome_button_config {
	struct gpio_dt_spec input;
};

static void publish_button(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	struct bthome_button_data *data =
		CONTAINER_OF(work, struct bthome_button_data, publish_work);

	LOG_DBG("publishing button press %d", data->curr_value);
	data->value = data->curr_value;
	data->curr_value = BTHOME_VALUE_BUTTON_NONE;
	data->event = (data->value != BTHOME_VALUE_BUTTON_NONE);

	/* notify bthome, that data has changed */
	bthome_data_changed();

	if (data->event) {
		k_work_reschedule(work, K_SECONDS(CONFIG_BTHOME_BUTTON_SENSOR_EVENT_DURATION));
	}
}

static void binary_sensor_triggered(const struct device *dev, struct gpio_callback *cb,
				    uint32_t pins)
{
	struct bthome_button_data *data = CONTAINER_OF(cb, struct bthome_button_data, callback);
	gpio_port_value_t value;
	int err;
	uint64_t delta;

	/* since we get a pinmask and not a single pin, we can't use the gpio_pin_get function here.
	 */
	err = gpio_port_get(dev, &value);
	if (err) {
		LOG_ERR("Could not read GPIO value.");
		return;
	}

	delta = k_uptime_delta(&data->push_time);
	if (delta < 10) { /* filter out bounces */
		return;
	}

	if (value & pins) {
		if (delta > CONFIG_BTHOME_BUTTON_SENSOR_IDLE_TIME) {
			LOG_DBG("Button pressed");
			data->curr_value = BTHOME_VALUE_BUTTON_PRESSED;
		} else { /* filter out bounces */
			LOG_DBG("Button pressed again");
			if (data->curr_value != BTHOME_VALUE_BUTTON_TRIPLE_PRESS &&
			    data->curr_value != BTHOME_VALUE_BUTTON_LONG_TRIPLE_PRESS) {
				/* increase press count */
				data->curr_value++;
			}
			k_work_cancel_delayable(&data->publish_work);
		}
	} else {
		if (delta > CONFIG_BTHOME_BUTTON_SENSOR_LONG_PRESS) {
			LOG_DBG("Button released after long press");
			if (data->curr_value < BTHOME_VALUE_BUTTON_LONG_PRESS) {
				data->curr_value += 3;
			}
		} else {
			LOG_DBG("Button released after short press");
		}

		k_timeout_t publish = K_MSEC(CONFIG_BTHOME_BUTTON_SENSOR_IDLE_TIME);
		if (data->curr_value == BTHOME_VALUE_BUTTON_TRIPLE_PRESS ||
		    data->curr_value == BTHOME_VALUE_BUTTON_LONG_TRIPLE_PRESS) {
			/* there is no quadruple-press we need to wait for. publish now. */
			publish = K_NO_WAIT;
		}
		k_work_reschedule(&data->publish_work, publish);
	}
}

static int bthome_button_init(const struct device *dev)
{
	const struct bthome_button_config *cfg = dev->config;
	struct bthome_button_data *data = dev->data;
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

#define BTHOME_BUTTON_DEFINE(inst)                                                                 \
	static struct bthome_button_data bthome_button_driver_##inst = {                           \
		.value = BTHOME_VALUE_BUTTON_NONE,                                                 \
		.curr_value = BTHOME_VALUE_BUTTON_NONE,                                            \
		.event = false,                                                                    \
		.publish_work = Z_WORK_DELAYABLE_INITIALIZER(publish_button),                      \
	};                                                                                         \
                                                                                                   \
	BTHOME_DEFINE_SENSOR(bthome_button_##inst, BTHOME_SENSOR_BUTTON,                           \
			     &bthome_button_driver_##inst.value, 1,                                \
			     &bthome_button_driver_##inst.event);                                  \
                                                                                                   \
	static const struct bthome_button_config bthome_button_cfg_##inst = {                      \
		.input = GPIO_DT_SPEC_INST_GET(inst, input_gpios),                                 \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, bthome_button_init, NULL, &bthome_button_driver_##inst,        \
			      &bthome_button_cfg_##inst, POST_KERNEL,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(BTHOME_BUTTON_DEFINE)
