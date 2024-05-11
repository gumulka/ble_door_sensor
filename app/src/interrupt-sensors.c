#include "interrupt-sensors.h"

#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(interrupt_sensors, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec hall_sensor = GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios);
static struct gpio_callback hall_sensor_callback;
static struct gpio_callback button_callback;
static struct k_work_delayable *read_sensors_work;

static void hall_sensor_triggered(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_work_reschedule(read_sensors_work, Z_TIMEOUT_MS(200));
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_work_reschedule(read_sensors_work, Z_TIMEOUT_MS(200));
}

static int configure_interrupt_gpio(const struct gpio_dt_spec *hall_sensor)
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

	LOG_DBG("Set up gpio at %s pin %d", hall_sensor->port->name, hall_sensor->pin);
	return 0;
}

int int_init_sensors(struct k_work_delayable *sensors_read)
{
	read_sensors_work = sensors_read;

	int ret;

	ret = configure_interrupt_gpio(&hall_sensor);
	if (ret < 0) {
		return ret;
	}
	gpio_init_callback(&hall_sensor_callback, hall_sensor_triggered, BIT(hall_sensor.pin));
	gpio_add_callback(hall_sensor.port, &hall_sensor_callback);

	ret = configure_interrupt_gpio(&button);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&button_callback, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_callback);

	return ret;
}

int int_read_sensor_data(uint8_t *button_val, uint8_t *window)
{
	*window = !gpio_pin_get_dt(&hall_sensor);
	*button_val = !!gpio_pin_get_dt(&button);
	LOG_DBG("Sensor values: %d, %d", *window, *button_val);
    return 0;
}
