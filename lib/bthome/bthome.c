#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>
#include <host/adv.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bthome, CONFIG_BTHOME_LOG_LEVEL);

#include <lib/bthome.h>

static uint8_t service_data[BT_GAP_ADV_MAX_ADV_DATA_LEN] = {0};
static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	/* this last entry needs to be updated with correct length */
	BT_DATA(BT_DATA_SVC_DATA16, service_data, BT_GAP_ADV_MAX_ADV_DATA_LEN)};

K_SEM_DEFINE(bthome_update_sem, 0, 1);

const struct bt_le_adv_param *slow_adv_param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY, CONFIG_BTHOME_NORMAL_INTERVAL / 2,
			CONFIG_BTHOME_NORMAL_INTERVAL, NULL);

const struct bt_le_adv_param *fast_adv_param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY, CONFIG_BTHOME_FAST_INTERVAL / 2,
			CONFIG_BTHOME_FAST_INTERVAL, NULL);

#ifdef CONFIG_BTHOME_BLINK_LED
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

static void update_ble_adv_data(void)
{
	int ret;
	uint8_t *ptr;
	bool event = false, prev_event;
	LOG_INF("BLE Update Thread started!");
	const struct bt_le_adv_param *param;
	while (true) {
#ifdef CONFIG_BTHOME_BLINK_LED
		gpio_pin_set_dt(&led, 0);
#endif

		k_sem_take(&bthome_update_sem, K_FOREVER);

#ifdef CONFIG_BTHOME_BLINK_LED
		gpio_pin_set_dt(&led, 1);
#endif

		/* first three bytes are UUID and INFO*/
		ptr = service_data + 3;
		prev_event = event;
		event = false;
		/* collect the data from all sensors and update the service_data array */
		STRUCT_SECTION_FOREACH(bthome_sensor, sensor) {
			*ptr++ = sensor->bthome_id;
			memcpy(ptr, sensor->data, sensor->length);
			ptr += sensor->length;
			event |= *(sensor->event);
		}

		LOG_HEXDUMP_DBG(service_data, ptr - service_data, "Service data:");

		if (event == prev_event) {
			/* Update the advertising data */
			ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
			if (ret) {
				LOG_ERR("Failed to update advertising data (err %d)", ret);
				continue;
			}
			LOG_DBG("Advertising data updated");
		} else {
			LOG_DBG("Event state changed, updating advertising parameters");
			if (event) {
				param = fast_adv_param;
			} else {
				param = slow_adv_param;
			}
			ret = bt_le_adv_stop();
			if (ret) {
				printk("Advertising failed to stop (err %d)\n", ret);
				continue;
			}
			/* Start advertising */
			ret = bt_le_adv_start(param, ad, ARRAY_SIZE(ad), NULL, 0);
			if (ret) {
				LOG_ERR("Advertising failed to start (err %d)", ret);
				continue;
			}
			LOG_DBG("Restarted BT Advertising with new params and data.");
		}
	}
}

K_THREAD_DEFINE(bthome_thread_id, CONFIG_BTHOME_STACK_SIZE, update_ble_adv_data, NULL, NULL, NULL,
		7, 0, 0);

void bthome_data_changed(void)
{
	k_sem_give(&bthome_update_sem);
}

static int bthome_init(void)
{
	int ret;
	int size = 3; /* 2 bytes for UUID, 1 byte for info */

	STRUCT_SECTION_FOREACH(bthome_sensor, sensor) {
		/* 1 byte for sensor ID + length of sensor data */
		size += 1 + sensor->length;
	}

	if (size == 3) {
		LOG_ERR("No sensors defined, cannot create service data");
		return -EINVAL;
	}

	if (size > BT_GAP_ADV_MAX_ADV_DATA_LEN) {
		LOG_ERR("Service data size exceeds maximum advertising data size");
		return -ENOMEM;
	}

#ifdef CONFIG_BTHOME_BLINK_LED
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("No LED defined");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Could not configure LED.");
		return -EINVAL;
	}
#endif

	/* update the length of service data to reflect reality. */
	ad[ARRAY_SIZE(ad) - 1].data_len = size;

	service_data[0] = (uint8_t)(CONFIG_BTHOME_SERVICE_UUID & 0xFF);
	service_data[1] = (uint8_t)((CONFIG_BTHOME_SERVICE_UUID >> 8) & 0xFF);
	service_data[2] =
		BTHOME_INFO_VERSION | BTHOME_INFO_REGULAR_INTERVAL | BTHOME_INFO_UNENCRYPTED_DATA;

	uint8_t *ptr = service_data + 3;
	STRUCT_SECTION_FOREACH(bthome_sensor, sensor) {
		*ptr++ = sensor->bthome_id;
		memcpy(ptr, sensor->data, sensor->length);
		ptr += sensor->length;
	}

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(NULL);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	/* Start advertising */
	ret = bt_le_adv_start(slow_adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		LOG_ERR("Advertising failed to start (err %d)", ret);
		return ret;
	}

	k_thread_start(bthome_thread_id);

	LOG_DBG("Init done for BTHome");
	return 0;
}
SYS_INIT(bthome_init, APPLICATION, 99);
