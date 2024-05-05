# Bluetooth Window/Door Sensor

A simple project to observe a window or door is open/closed.
It works like most other sensors as well. A magnet has to get attached to the door and
if it comes near the board, it is detected and the state of the window or door is updated.

## How to build

If you are new to Zephyr, read the [Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).
Afterwards you can clone and build this project with:

```bash
west init -m https://github.com/gumulka/ble_door_sensor --mr main door_sensor
cd door_sensor
west update
cd ble_door_sensor
```

### Bootloader

The bootloader will load and verify the image that is executed. To make it
easier to upgrade the device, two images are saved on the device and only one is
being executed, while the other is a standby for updating. The bootloader cannot
be upgraded via ble, but the image can be. Therefore making it possible to
upgrade devices in the field.

If you have a unprogrammed device, use this to build and flash the bootloader:

```bash
west build -p -b ble_door_sensor -d build_mcuboot ../bootloader/mcuboot/boot/zephyr/ -- -DCONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
west flash -d build_mcuboot
```

### Application

The application is different from the bootloader and is what is in need of
updates/upgrades.

```bash
west build -p -b ble_door_sensor app -- -DOVERLAY_CONFIG="output.conf"
west flash
```

## Update via BLE

If you have an already running device and just want to upgrade to the latest
firmware.

Either with the nRF Connect App, which can be quite handy, or in the commandline
use these steps:

```bash
sudo mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='Sensor' image upload build/zephyr/zephyr.signed.bin
sudo mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='Sensor' image list
```

Use the second hash provided from the second command to alter the following:

```bash
sudo mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='Sensor' image test "HASH_FROM_OUTPUT"
sudo mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='Sensor' reset
# wait for the system to restart and if you are sure, that the update was successful then
sudo mcumgr  --conntype ble --connstring ctlr_name=hci0,peer_name='Sensor' image confirm "HASH_FROM_OUTPUT"
```

More information about the general procedure can be found [here](https://docs.zephyrproject.org/latest/samples/subsys/mgmt/mcumgr/smp_svr/README.html)
and [here](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html)

> [!TIP]
> I have seen a few errors when trying to upload the image and found that it
> works best, if I cancel the upload command after a few seconds and restart it
> a few seconds later, basically splitting the upload into 4-5 chunks instead
> of uploading it as a whole.
