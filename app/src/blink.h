/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_DRIVERS_BLINK_H_
#define APP_DRIVERS_BLINK_H_

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

struct bt_data;

int energy_init(const struct device *dev,
				struct bt_data *bt_data, size_t data_size, uint32_t *energy_wh);

#endif /* APP_DRIVERS_BLINK_H_ */