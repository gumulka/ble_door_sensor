#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

int main(void) {

  LOG_INF("BLE Door Sensor");

  return 0;
}
