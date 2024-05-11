#ifndef INTERRUPT_SENSORS_H
#define INTERRUPT_SENSORS_H

#include <zephyr/kernel.h>

int int_init_sensors(struct k_work_delayable *read_sensors_work);

int int_read_sensor_data(uint8_t *button, uint8_t *window);

#endif /* INTERRUPT_SENSORS_H */
