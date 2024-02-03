#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec hall_sensor_left =
	GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor_left), gpios);
static const struct gpio_dt_spec hall_sensor_right =
	GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor_right), gpios);
static struct gpio_callback hall_sensor_left_callback;
static struct gpio_callback hall_sensor_right_callback;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	LOG_INF("Hall sensor edge");
}

int configure_sensor(const struct gpio_dt_spec *hall_sensor,
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
	LOG_INF("Set up button at %s pin %d", hall_sensor->port->name, hall_sensor->pin);
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

	LOG_INF("Ready");

	return 0;
}
