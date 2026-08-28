#ifndef BT_HOME_H
#define BT_HOME_H

#define BTHOME_INFO_VERSION            0x40
#define BTHOME_INFO_REGULAR_INTERVAL   0x00
#define BTHOME_INFO_IRREGULAR_INTERVAL 0x04
#define BTHOME_INFO_ENCRYPTED_DATA     0x01
#define BTHOME_INFO_UNENCRYPTED_DATA   0x00

#define BTHOME_SENSOR_BATTERY        0x01
#define BTHOME_SENSOR_TEMPERATURE    0x02
#define BTHOME_SENSOR_HUMIDITY_16    0x03
#define BTHOME_SENSOR_ILLUMINANCE    0x05
#define BTHOME_SENSOR_BINARY_BATTERY 0x15
#define BTHOME_SENSOR_BINARY_DOOR    0x1A
#define BTHOME_SENSOR_BINARY_WINDOW  0x2D
#define BTHOME_SENSOR_HUMIDITY_8     0x2E
#define BTHOME_SENSOR_BUTTON         0x3A
#define BTHOME_SENSOR_ENERGY_WH      0x4D
#define BTHOME_SENSOR_POWER_CW_24    0x0B

#define BTHOME_VALUE_DOOR_CLOSED              0x00
#define BTHOME_VALUE_DOOR_OPEN                0x01
#define BTHOME_VALUE_BUTTON_NONE              0x00
#define BTHOME_VALUE_BUTTON_PRESSED           0x01
#define BTHOME_VALUE_BUTTON_DOUBLE_PRESS      0x02
#define BTHOME_VALUE_BUTTON_TRIPLE_PRESS      0x03
#define BTHOME_VALUE_BUTTON_LONG_PRESS        0x04
#define BTHOME_VALUE_BUTTON_LONG_DOUBLE_PRESS 0x05
#define BTHOME_VALUE_BUTTON_LONG_TRIPLE_PRESS 0x06
#define BTHOME_VALUE_BUTTON_HOLD_PRESS        0x80
#define BTHOME_VALUE_WINDOW_CLOSED            0x00
#define BTHOME_VALUE_WINDOW_OPEN              0x01

struct bthome_sensor {
	uint8_t bthome_id;
	uint8_t *data;
	size_t length;
	bool *event;
};

#define BTHOME_DEFINE_SENSOR(name, _id, _data, _length, _event)                                    \
	static const STRUCT_SECTION_ITERABLE(bthome_sensor, bthome_sensor_##name) = {              \
		.bthome_id = _id,                                                                  \
		.data = _data,                                                                     \
		.length = _length,                                                                 \
		.event = _event,                                                                   \
	}

void bthome_data_changed(void);

#endif /* BT_HOME_H */
