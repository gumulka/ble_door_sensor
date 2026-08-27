#include <zephyr/kernel.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/pm/device_runtime.h>

#include <lib/bthome.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT energy_tracker_special

LOG_MODULE_REGISTER(energy_tracker, CONFIG_BTHOME_SENSORS_LOG_LEVEL);

struct energy_tracker_data {
	uint32_t energy;
	uint32_t power;
	bool event;
	int64_t time_ref;
};

struct energy_tracker_config {
	struct gpio_dt_spec enable;
	const struct device *comp_dev;
};

static void comp_callback(const struct device *dev, void *user_data)
{
	struct energy_tracker_data *data = (struct energy_tracker_data *)user_data;
	int64_t now = k_uptime_get();
	int64_t delta;

	/* The LPCOMP seems to trigger on both edges, even though we have configured it for falling
	 * edge only. So we check the output here and only count if it is low. */
	if (comparator_get_output(dev)) {
		return;
	}

	delta = now - data->time_ref;

	if (delta < 10) {
		return;
	}

	data->time_ref = now;

	data->energy++;
	LOG_DBG("Energy is now %d.%03d kWh", data->energy / 1000, data->energy % 1000);

	/* each callback is 1Wh. Time delta is in ms.
	 * 1WH = 3600Ws = 3600*1000Wms
	 * We calculate the power in 0.01W, since BThome wants it. */
	int64_t power = (3600 * 1000 * 100) / delta;
	LOG_DBG("Calculated power: %lld.%02d W", power / 100, (int)power % 100);
	data->power = power;

	/* notify bthome, that data has changed */
	bthome_data_changed();
}

static int energy_tracker_init(const struct device *dev)
{
	const struct energy_tracker_config *cfg = dev->config;
	struct energy_tracker_data *data = dev->data;
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

	if (!device_is_ready(cfg->comp_dev)) {
		LOG_ERR("No comparator defined");
		return -ENODEV;
	}

	data->time_ref = k_uptime_get();

	if (comparator_set_trigger_callback(cfg->comp_dev, comp_callback, data) != 0) {
		LOG_ERR("Could not set comparator callback");
		return -EIO;
	}

	if (comparator_set_trigger(cfg->comp_dev, COMPARATOR_TRIGGER_FALLING_EDGE) != 0) {
		LOG_ERR("Could not set comparator trigger");
		return -EIO;
	}

	/* prevent the device from getting shut down. */
	err = pm_device_runtime_get(cfg->comp_dev);
	if (err) {
		LOG_ERR("Could not get runtime for comparator device (%d)", err);
		return err;
	}
	LOG_DBG("Energy Tracker initalized");

	return 0;
}

#define ENERGY_TRACKER_DEFINE(inst)                                                                \
	static struct energy_tracker_data energy_tracker_driver_##inst = {                         \
		.energy = 0,                                                                       \
		.power = 0,                                                                        \
		.event = false,                                                                    \
	};                                                                                         \
                                                                                                   \
	COND_CODE_1(DT_INST_PROP(inst, include_energy),                                            \
		    (BTHOME_DEFINE_SENSOR(energy_tracker_e_##inst, BTHOME_SENSOR_ENERGY_WH,        \
					  (uint8_t *)&energy_tracker_driver_##inst.energy, 4,      \
					  &energy_tracker_driver_##inst.event)),                   \
		    ());                                                                           \
                                                                                                   \
	COND_CODE_1(DT_INST_PROP(inst, include_calculated_power),                                  \
		    (BTHOME_DEFINE_SENSOR(energy_tracker_p_##inst, BTHOME_SENSOR_POWER_CW_24,      \
					  (uint8_t *)&energy_tracker_driver_##inst.power, 3,       \
					  &energy_tracker_driver_##inst.event)),                   \
		    ());                                                                           \
                                                                                                   \
	BUILD_ASSERT(DT_INST_PROP(inst, include_energy) ||                                         \
			     DT_INST_PROP(inst, include_calculated_power),                         \
		     "Either power or energy must be part of the output");                         \
                                                                                                   \
	static const struct energy_tracker_config energy_tracker_cfg_##inst = {                    \
		.enable = GPIO_DT_SPEC_INST_GET(inst, enable_gpios),                               \
		.comp_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, comp)),                            \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, energy_tracker_init, NULL, &energy_tracker_driver_##inst,      \
			      &energy_tracker_cfg_##inst, POST_KERNEL,                             \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(ENERGY_TRACKER_DEFINE)
