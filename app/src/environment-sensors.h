#ifndef ENVIRONMENT_SENSORS_H
#define ENVIRONMENT_SENSORS_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

int battery_init(struct bt_data *data, size_t data_size, uint8_t *battery);

#endif /* ENVIRONMENT_SENSORS_H */
