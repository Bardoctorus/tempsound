/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/print_format.h>


#include <zephyr/logging/log.h>



#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>

//nordic buttons and leds for testing 
#include <dk_buttons_and_leds.h>
#define USER_BUTTON DK_BTN1_MASK

//company code is Nordic for now
#define COMPANY_CODE 0x0059

typedef struct adv_mfg_data {
	uint16_t company_code;
	uint16_t number_press;
} adv_mfg_data_type;

static const struct bt_le_adv_param *adv_param = 
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_NONE, // no avertising options selected
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
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	//advertising packet data
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};


// dummy url data
static unsigned char url_data[] = { 0x17, '/', '/', 'x', 'k', 'c', 'd', '.', 'c',
				    'o',  'm' };

static const struct bt_data sd[] ={
	//scan response packet - dummy url
	BT_DATA(BT_DATA_URI, url_data,sizeof(url_data)),
};				

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

	// cheking bme device is all the things
	const struct device *dev = check_bme280_device();
	if (dev == NULL) {
		return 0;
	}

	//enable bluetooth
	err = bt_enable(NULL);
	if(err){
		LOG_ERR("Couldnt start bluetooth error: %d", err);
		return -1;
	}
	LOG_INF("Bluetooth Started");

	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if(err){
		LOG_ERR("advertising did not start- error: %d", err);
		return -1;
	}

	LOG_INF("Advertising Started");


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
