#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

#include <lib/bthome.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT bthome_battery

LOG_MODULE_REGISTER(bthome_battery, CONFIG_BTHOME_SENSORS_LOG_LEVEL);

struct bthome_battery_data {
	uint8_t percent;
	bool event;
	struct k_work_delayable work;
	const struct device *dev;
};

struct bthome_battery_config {
	const struct adc_dt_spec adc_channel;
	uint32_t full;
	uint32_t empty;
};

static void battery_sensor_update(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_SECONDS(CONFIG_BTHOME_BATTERY_SENSOR_UPDATE_INTERVAL));
	struct bthome_battery_data *data = CONTAINER_OF(work, struct bthome_battery_data, work);
	const struct bthome_battery_config *cfg = data->dev->config;

	uint16_t buf;
	int err;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};

	adc_sequence_init_dt(&cfg->adc_channel, &sequence);

	err = adc_read_dt(&cfg->adc_channel, &sequence);
	if (err < 0) {
		printk("Could not read ADC (%d)\n", err);
		return;
	}
	int32_t batt = (int32_t)buf;
	adc_raw_to_millivolts_dt(&cfg->adc_channel, &batt);
	// convert mv to percentage with 3V beeing 100% and 2V beeing 0%
	// This is not a battery curve, just some calculations for better or worse.
	batt -= cfg->empty;
	batt = batt * 100 / (cfg->full - cfg->empty);
	if (batt > 100) {
		batt = 100;
	} else if (batt < 0) {
		batt = 0;
	}
	LOG_DBG("Battery is at %d percent", batt);
	if (batt == data->percent) {
		return;
	}
	data->percent = (uint8_t)batt;

	/* notify bthome, that data has changed */
	bthome_data_changed();
}

static int bthome_battery_init(const struct device *dev)
{
	const struct bthome_battery_config *cfg = dev->config;
	struct bthome_battery_data *data = dev->data;
	int err;

	data->dev = dev;

	if (!adc_is_ready_dt(&cfg->adc_channel)) {
		LOG_ERR("ADC controller device is not ready\n");
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&cfg->adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup channel err(%d)\n", err);
		return err;
	}

	battery_sensor_update(&data->work.work);

	return 0;
}

#define BTHOME_BATTERY_DEFINE(inst)                                                                \
	static struct bthome_battery_data bthome_battery_driver_##inst = {                         \
		.percent = 0,                                                                      \
		.event = false,                                                                    \
		.work = Z_WORK_DELAYABLE_INITIALIZER(battery_sensor_update),                       \
	};                                                                                         \
                                                                                                   \
	BTHOME_DEFINE_SENSOR(bthome_battery_##inst, BTHOME_SENSOR_BATTERY,                         \
			     &bthome_battery_driver_##inst.percent, 1,                             \
			     &bthome_battery_driver_##inst.event);                                 \
                                                                                                   \
	static const struct bthome_battery_config bthome_battery_cfg_##inst = {                    \
		.adc_channel = ADC_DT_SPEC_INST_GET(inst),                                         \
		.full = DT_INST_PROP(inst, voltage_full),                                          \
		.empty = DT_INST_PROP(inst, voltage_empty),                                        \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, bthome_battery_init, NULL, &bthome_battery_driver_##inst,      \
			      &bthome_battery_cfg_##inst, POST_KERNEL,                             \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(BTHOME_BATTERY_DEFINE)
