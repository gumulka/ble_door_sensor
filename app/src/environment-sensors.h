#ifndef ENVIRONMENT_SENSORS_H
#define ENVIRONMENT_SENSORS_H

#include <zephyr/kernel.h>

int env_init_sensors();

int env_read_sensor_data(uint8_t *battery, uint8_t *temperature, uint8_t *humidity,
			 uint8_t *illuminance);

#endif /* ENVIRONMENT_SENSORS_H */
