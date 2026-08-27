#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

#include <lib/bthome.h>

#include <zephyr/logging/log.h>
#define DT_DRV_COMPAT bthome_temp_humidity

LOG_MODULE_REGISTER(bthome_temp_humidity, CONFIG_BTHOME_SENSORS_LOG_LEVEL);

struct bthome_temp_humidity_data {
	uint8_t temperature[2];
	uint8_t humidity;
	bool event;
	struct k_work_delayable work;
	const struct device *sensor;
};

static void temp_humidity_sensor_update(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);
	k_work_reschedule(work, K_SECONDS(CONFIG_BTHOME_TEMP_HUMIDITY_SENSOR_UPDATE_INTERVAL));
	struct bthome_temp_humidity_data *data =
		CONTAINER_OF(work, struct bthome_temp_humidity_data, work);

	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch_chan(data->sensor, SENSOR_CHAN_ALL);
	if (ret < 0) {
		LOG_WRN("Could not fetch shtc data: %d", ret);
		return;
	}

	ret = sensor_channel_get(data->sensor, SENSOR_CHAN_AMBIENT_TEMP, &value);
	if (ret < 0) {
		LOG_WRN("Could not get temperature: %d", ret);
		return;
	}
	LOG_DBG("Temperature is %d°C", value.val1);
	uint16_t temp = (uint16_t)(value.val1 * 100 + value.val2 / 10000);
	data->temperature[0] = temp & 0xFF;
	data->temperature[1] = (temp >> 8) & 0xFF;

	ret = sensor_channel_get(data->sensor, SENSOR_CHAN_HUMIDITY, &value);
	if (ret < 0) {
		LOG_WRN("Could not get humidity: %d", ret);
		return;
	}
	LOG_DBG("Humidity is %d percent", value.val1);
	data->humidity = (uint8_t)(value.val1 & 0xFF);

	/* notify bthome, that data has changed */
	bthome_data_changed();
}

static int bthome_temp_humidity_init(const struct device *dev)
{
	struct bthome_temp_humidity_data *data = dev->data;

	if (!device_is_ready(data->sensor)) {
		LOG_ERR("Sensor is not ready\n");
		return -ENODEV;
	}

	temp_humidity_sensor_update(&data->work.work);

	return 0;
}

#define BTHOME_TEMP_HUMIDITY_DEFINE(inst)                                                          \
	static struct bthome_temp_humidity_data bthome_temp_humidity_driver_##inst = {             \
		.temperature = {0, 0},                                                             \
		.humidity = 0,                                                                     \
		.event = false,                                                                    \
		.work = Z_WORK_DELAYABLE_INITIALIZER(temp_humidity_sensor_update),                 \
		.sensor = DEVICE_DT_GET(DT_INST_PHANDLE(inst, sensor)),                            \
	};                                                                                         \
                                                                                                   \
	COND_CODE_1(                                                                               \
		DT_INST_PROP(inst, include_temperature),                                           \
		(BTHOME_DEFINE_SENSOR(bthome_temp_humidity_t_##inst, BTHOME_SENSOR_TEMPERATURE,    \
				      bthome_temp_humidity_driver_##inst.temperature, 2,           \
				      &bthome_temp_humidity_driver_##inst.event)),                 \
		());                                                                               \
                                                                                                   \
	COND_CODE_1(DT_INST_PROP(inst, include_humidity),                                          \
		    (BTHOME_DEFINE_SENSOR(bthome_temp_humidity_h_##inst, BTHOME_SENSOR_HUMIDITY_8, \
					  &bthome_temp_humidity_driver_##inst.humidity, 1,         \
					  &bthome_temp_humidity_driver_##inst.event)),             \
		    ());                                                                           \
                                                                                                   \
	BUILD_ASSERT(DT_INST_PROP(inst, include_temperature) ||                                    \
			     DT_INST_PROP(inst, include_humidity),                                 \
		     "Either temperature or humidity must be part of the output");                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, bthome_temp_humidity_init, NULL,                               \
			      &bthome_temp_humidity_driver_##inst, NULL, POST_KERNEL,              \
			      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(BTHOME_TEMP_HUMIDITY_DEFINE)
