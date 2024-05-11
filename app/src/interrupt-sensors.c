#include "interrupt-sensors.h"

#include "bthome.h"

#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(interrupt_sensors, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec hall_sensor = GPIO_DT_SPEC_GET(DT_NODELABEL(hall_sensor), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios);
static struct gpio_callback hall_sensor_callback;
static struct gpio_callback button_callback;
static struct k_work_delayable *read_sensors_work;

static uint8_t button_value = BTHOME_VALUE_BUTTON_NONE;

static int64_t time_last_button_action;

static void stop_button_cb(struct k_work *_work)
{
    LOG_DBG("Stopping button press.");
    // we have broadcasted our button press long enough.
    button_value = BTHOME_VALUE_BUTTON_NONE;
	k_work_reschedule(read_sensors_work, K_NO_WAIT);
}
K_WORK_DELAYABLE_DEFINE(stop_button_work, stop_button_cb);

static void button_cb(struct k_work *_work)
{
    int64_t difference = sys_clock_tick_get() - time_last_button_action;
    LOG_DBG("Time difference: %lld", difference);

    if (difference >= k_ms_to_ticks_ceil32(4000)) {
        button_value = BTHOME_VALUE_BUTTON_LONG_PRESS;
        LOG_DBG("Sending long press");
    } else {
        button_value = BTHOME_VALUE_BUTTON_PRESSED;
        LOG_DBG("Sending short press");
    }

    // stop the current button symbol in 1 minute
    k_work_reschedule(&stop_button_work, K_MINUTES(1));
    // but start broadcasting the current button symbol now
	k_work_reschedule(read_sensors_work, K_NO_WAIT);
}
K_WORK_DELAYABLE_DEFINE(button_work, button_cb);

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(cb);
    int state = gpio_pin_get_dt(&button);
    LOG_DBG("Button interrupt with value %d", state);
    switch (state)
    {
    case 1:
        time_last_button_action = sys_clock_tick_get();
        // 4 seconds is long enough for long button press.
        k_work_reschedule(&button_work, K_SECONDS(4));
        return;
    case 0:
        int64_t difference = sys_clock_tick_get() - time_last_button_action;
        if(difference < k_ms_to_ticks_ceil32(4000)) {
            // if it is after 4 seconds, then we have already send a long press.
            k_work_reschedule(&button_work, K_NO_WAIT);
        }
        return;

    default:
        break;
    }
    if(state < 0) {
        return;
    }
}

static void hall_sensor_triggered(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
    // delay by a bit to account for bouncing effects.
	k_work_reschedule(read_sensors_work, Z_TIMEOUT_MS(100));
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

    time_last_button_action = sys_clock_tick_get();

	return ret;
}

int int_read_sensor_data(uint8_t *button_val, uint8_t *window)
{
    int ret = 0;
    switch (gpio_pin_get_dt(&hall_sensor))
    {
    case 0:
        *window = BTHOME_VALUE_WINDOW_OPEN;
        break;
    case 1:
        *window = BTHOME_VALUE_WINDOW_CLOSED;
        break;
    default:
        ret = -EIO;
        break;
    }
	*button_val = button_value;
	LOG_DBG("Sensor values: %d, %d", *window, *button_val);
    return 0;
}
