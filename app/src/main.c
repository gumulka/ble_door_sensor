#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/comparator.h>

#include <zephyr/pm/device_runtime.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#include "bthome.h"
#include "environment-sensors.h"

static uint8_t service_data[] = {
	BT_UUID_16_ENCODE(BTHOME_SERVICE_UUID),
	BTHOME_INFO_VERSION | BTHOME_INFO_IRREGULAR_INTERVAL | BTHOME_INFO_UNENCRYPTED_DATA,
	BTHOME_SENSOR_BATTERY,
	0,
	BTHOME_SENSOR_ENERGY_WH,
	0,
	0,
	0,
	0,
	BTHOME_SENSOR_POWER_10MW_24BIT,
	0,
	0,
	0,
};

#define POS_BATTERY_DATA     4
#define POS_ENERGY_WH_DATA   6
#define POS_ENERGY_POWER_DATA   11

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data))};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec comp_en = GPIO_DT_SPEC_GET(DT_ALIAS(compen), gpios);
static const struct device *comp_dev = DEVICE_DT_GET(DT_NODELABEL(comp));

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Rebooting system");

	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	/* Start advertising */
	err = bt_le_adv_start(
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
				BT_LE_ADV_INTERVAL_MAX / 2, BT_LE_ADV_INTERVAL_MAX, NULL),
		ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
}

static struct k_sem comp_sem;
int64_t time_ref = 0;

static void comp_callback(const struct device *dev, void *user_data)
{
	int64_t now = k_uptime_get();
	uint32_t *counter = (uint32_t*)(service_data + POS_ENERGY_WH_DATA);
	int64_t delta;

	// The LPCOMP seems to trigger on both edges, even though we have configured it for falling edge only. So we check the output here and only count if it is low.
	if(comparator_get_output(dev)) {
		return;
	}

	delta = now - time_ref;

	if(delta < 10) {
		return;
	}

	time_ref = now;

	(*counter)++;

	// each callback is 1Wh. Time delta is in ms.
	// 1WH = 3600Ws = 3600*1000Wms
	// We calculate the power in 0.01W, since BThome wants it.
	int64_t power = (3600 * 1000 * 100) / delta;
	uint8_t *power_ptr = service_data + POS_ENERGY_POWER_DATA;
	*power_ptr = power & 0xFF;
	power_ptr++;
	*power_ptr = (power >> 8) & 0xFF;
	power_ptr++;
	*power_ptr = (power >> 16) & 0xFF;

	// We are in interrupt context here. So no BLE update.
	k_sem_give(&comp_sem);
}

int main(void)
{
	int ret;
	uint32_t power;

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("No LED defined");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Could not configure LED.");
		return 0;
	}

	if (!gpio_is_ready_dt(&comp_en)) {
		LOG_ERR("No comparator enable defined");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&comp_en, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Could not configure comparator enable GPIO.");
		return 0;
	}

	if(!device_is_ready(comp_dev)) {
		LOG_ERR("No comparator defined");
		return -ENODEV;
	}

	k_sem_init(&comp_sem, 0, 1);
	time_ref = k_uptime_get();

	if(comparator_set_trigger_callback(comp_dev, comp_callback, NULL) != 0) {
		LOG_ERR("Could not set comparator callback");
		return -EIO;
	}

	if (comparator_set_trigger(comp_dev, COMPARATOR_TRIGGER_FALLING_EDGE) != 0) {
		LOG_ERR("Could not set comparator trigger");
		return -EIO;
	}

	/* Initialize the Bluetooth Subsystem */
	ret = bt_enable(bt_ready);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return ret;
	}

	battery_init(ad, ARRAY_SIZE(ad), service_data + POS_BATTERY_DATA);

	ret = pm_device_runtime_get(comp_dev);
	if (ret) {
		LOG_ERR("Could not get runtime for comparator device (%d)", ret);
		return ret;
	}

	// short blink to signal everything is okay
	for(int i = 0; i <3; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(80);
		gpio_pin_set_dt(&led, 0);
		k_msleep(100);
	}

	while(true) {
		k_sem_take(&comp_sem, K_FOREVER);
		power = service_data[POS_ENERGY_POWER_DATA] | (service_data[POS_ENERGY_POWER_DATA + 1] << 8) | (service_data[POS_ENERGY_POWER_DATA + 2] << 16);
		LOG_INF("Comparator triggered, energy: %d Wh, power: %d W", *(uint32_t*)(service_data + POS_ENERGY_WH_DATA), power/100);
		ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
		if (ret) {
			LOG_ERR("Failed to update advertising data (err %d)", ret);
		}
	}

	return 0;
}
