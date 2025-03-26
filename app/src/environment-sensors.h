#ifndef ENVIRONMENT_SENSORS_H
#define ENVIRONMENT_SENSORS_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

int read_shtc(const struct device *const shtc, uint8_t *temperature, uint8_t *humidity);
int read_supply_voltage(const struct adc_dt_spec *soc_voltage, uint8_t *battery);
int read_ambient_light(const struct device *const light, uint8_t *illuminance);

#endif /* ENVIRONMENT_SENSORS_H */
