/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 //general zephyr includes
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>


// logging and printing includes
#include <zephyr/dsp/print_format.h>
#include <zephyr/logging/log.h>


// bluetooth includes
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/addr.h>

//nordic buttons and leds for testing 
#include <dk_buttons_and_leds.h>
#include <bluetooth/services/lbs.h>
#define USER_BUTTON DK_BTN1_MSK
#define RUN_STATUS_LED DK_LED1
#define CONNECTION_STATUS_LED DK_LED2
#define RUN_LED_BLINK_INTERVAL 1000

//company code is Nordic for now
#define COMPANY_CODE 0x0059

typedef struct adv_mfg_data {
	uint16_t company_code;
	uint16_t number_press;
} adv_mfg_data_type;

// dummy url data
static unsigned char url_data[] = { 0x17, '/', '/', 'x', 'k', 'c', 'd', '.', 'c',
				    'o',  'm' };

static struct k_work adv_work;
struct bt_conn *my_conn = NULL;

static const struct bt_le_adv_param *adv_param = 
	BT_LE_ADV_PARAM((BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY), // advertisin options
		800, //min advertisins interval 500ms (800 *0.625)
		801, //max adv time 500.625ms (801 *0.625)
		NULL // undirected advertising
	);

static adv_mfg_data_type adv_mfg_data = { COMPANY_CODE, 0x00 };


LOG_MODULE_REGISTER(tempsound_logger, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) -1 )

static const struct bt_data ad[] = {
	//advertising flags
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	//advertising packet data
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	//manufacturer data 
	BT_DATA(BT_DATA_MANUFACTURER_DATA, (unsigned char*) &adv_mfg_data, sizeof(adv_mfg_data)),
};

static const struct bt_data sd[] ={
	//scan response packet - dummy url
	BT_DATA(BT_DATA_URI, url_data,sizeof(url_data)),

	// uuid of lbs service : https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/libraries/bluetooth/services/lbs.html#service-uuid
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_128_ENCODE(0x00001523, 0x1212, 0xefde, 0x1523, 0x785feabcd123)),

};	



static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}
static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

void on_connected(struct bt_conn *conn, uint8_t err){
	if(err){
		LOG_ERR("connection error, on_connected, err: %d", err);
		return;
	}
	LOG_INF("Connected");
	my_conn = bt_conn_ref(conn);

	dk_set_led(CONNECTION_STATUS_LED, 1);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason){
	
	LOG_INF("Disconnected because %d", reason);
	bt_conn_unref(my_conn);
	dk_set_led(CONNECTION_STATUS_LED, 0);

}


static void recycled_cb(void)
{
	printk("Connection object available from previous conn. Disconnect is complete!\n");
	advertising_start();
}


// These two things are the same. The difference is
// that here we are manually making the bt_conn_cb struct
// then registering them in main. The BT_CONN_CB_DEFINE macro
// will do both in one swipe. TODO: this.


// BT_CONN_CB_DEFINE(conn_callbacks) = {
// 	.recycled = recycled_cb,
// };

struct bt_conn_cb connection_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.recycled = recycled_cb,
};




		

static void button_changed(uint32_t button_state, uint32_t has_changed){

	// check for presses
	if (has_changed & button_state & USER_BUTTON){
		adv_mfg_data.number_press += 1;
		bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	}

	// check if pressed or released
	// this is the bare bones code from the nordic tutorial, no checking if notifications have been
	//selected or anything like that.
	int err;
	bool user_button_changed = (has_changed & USER_BUTTON) ? true : false;
	bool user_button_pressed = (button_state & USER_BUTTON) ? true : false;
	if (user_button_changed) {
		LOG_INF("Button %s", (user_button_pressed ? "pressed" : "released"));

		err = bt_lbs_send_button_state(user_button_pressed);
		if (err) {
			LOG_ERR("Couldn't send notification. (err: %d)", err);
		}
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}

/*
 * Get a device structure from a devicetree node with compatible
 * "bosch,bme280". (If there are multiple, just pick one.)
 */
const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme280);

SENSOR_DT_READ_IODEV(iodev, DT_COMPAT_GET_ANY_STATUS_OKAY(bosch_bme280),
		{SENSOR_CHAN_AMBIENT_TEMP, 0},
		{SENSOR_CHAN_HUMIDITY, 0},
		{SENSOR_CHAN_PRESS, 0});

RTIO_DEFINE(ctx, 1, 1);

static const struct device *check_bme280_device(void)
{
	if (dev == NULL) {
		/* No such node, or the node does not have status "okay". */
		printk("\nError: no device found.\n");
		return NULL;
	}

	if (!device_is_ready(dev)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       dev->name);
		return NULL;
	}

	printk("Found device \"%s\", getting sensor data\n", dev->name);
	return dev;
}

int main(void)
{
	int err;


	//nordic button and led setup tings
	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)\n", err);
		return -1;
	}
	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return -1;
	}

	// set static address
    bt_addr_le_t addr;

	err = bt_addr_le_from_str("FF:EE:DD:CC:BB:AA", "random", &addr);
	if (err) {
        printk("Invalid BT address (err %d)\n", err);
    }

    err = bt_id_create(&addr, NULL);
    if (err < 0) {
        printk("Creating new ID failed (err %d)\n", err);
    }

	err = bt_conn_cb_register(&connection_callbacks);
	if(err){
		LOG_ERR("connection callback registration failed: %d", err);
	}


	//enable bluetooth
	err = bt_enable(NULL);
	if(err){
		LOG_ERR("Couldnt start bluetooth error: %d", err);
		return -1;
	}
	LOG_INF("Bluetooth Started");

	// err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	// if(err){
	// 	LOG_ERR("advertising did not start- error: %d", err);
	// 	return -1;
	// }

	k_work_init(&adv_work, adv_work_handler);
	advertising_start();

	LOG_INF("Advertising Started");


		// cheking bme device is all the things
	const struct device *dev = check_bme280_device();
	if (dev == NULL) {
		return 0;
	}

	while (1) {
		uint8_t buf[128];

		int rc = sensor_read(&iodev, &ctx, buf, 128);

		if (rc != 0) {
			printk("%s: sensor_read() failed: %d\n", dev->name, rc);
			return rc;
		}

		const struct sensor_decoder_api *decoder;

		rc = sensor_get_decoder(dev, &decoder);

		if (rc != 0) {
			printk("%s: sensor_get_decode() failed: %d\n", dev->name, rc);
			return rc;
		}

		uint32_t temp_fit = 0;
		struct sensor_q31_data temp_data = {0};

		decoder->decode(buf,
			(struct sensor_chan_spec) {SENSOR_CHAN_AMBIENT_TEMP, 0},
			&temp_fit, 1, &temp_data);

		uint32_t press_fit = 0;
		struct sensor_q31_data press_data = {0};

		decoder->decode(buf,
				(struct sensor_chan_spec) {SENSOR_CHAN_PRESS, 0},
				&press_fit, 1, &press_data);

		uint32_t hum_fit = 0;
		struct sensor_q31_data hum_data = {0};

		decoder->decode(buf,
				(struct sensor_chan_spec) {SENSOR_CHAN_HUMIDITY, 0},
				&hum_fit, 1, &hum_data);

		printk("temp: %s%d.%d; press: %s%d.%d; humidity: %s%d.%d\n",
			PRIq_arg(temp_data.readings[0].temperature, 6, temp_data.shift),
			PRIq_arg(press_data.readings[0].pressure, 6, press_data.shift),
			PRIq_arg(hum_data.readings[0].humidity, 6, hum_data.shift));

		k_sleep(K_MSEC(1000));
	}
	return 0;
}
