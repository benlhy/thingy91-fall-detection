/*
 * Copyright (c) 2019 Brett Witherspoon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <devicetree.h>
#include <drivers/gpio.h>
//#include <stdio.h>
#include <device.h>
#include <drivers/sensor.h>




/* The devicetree node identifier for the nodelabel in the dts file. */
#define SW0_NODE DT_ALIAS(sw0)

#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;


/* The devicetree node identifier for the nodelabel in the dts file. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0	DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN		DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS	DT_GPIO_FLAGS(LED0_NODE, gpios)
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0	""
#define PIN	0
#define FLAGS	0
#endif

K_SEM_DEFINE(sem, 0, 1);

bool collect_data_flag;
int count;

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	collect_data_flag = true;
	count = 0;
	// printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());

}

static void trigger_handler(const struct device *dev,
			    const struct sensor_trigger *trig)
{
	switch (trig->type) {
	case SENSOR_TRIG_DATA_READY:
		if (sensor_sample_fetch(dev) < 0) {
			printf("Sample fetch error\n");
			return;
		}
		k_sem_give(&sem);
		break;
	case SENSOR_TRIG_THRESHOLD:
		printf("Threshold trigger\n");
		break;
	default:
		printf("Unknown trigger\n");
	}
}

void main(void)
{
	struct sensor_value accel[3];


	int ret;
	bool led_on = true;
	const struct device *leddev;
	leddev = device_get_binding(LED0);
	if (leddev==NULL) {
		printk("No device found!");
		return;
	}
	ret = gpio_pin_configure(leddev,PIN, GPIO_OUTPUT_ACTIVE | FLAGS);

	if (ret<0 ){
		return;
	}

	gpio_pin_set(leddev, PIN, true);

	
	collect_data_flag = false;
	count = 0;

	
	if (!device_is_ready(button.port)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);


	
	

	const struct device *dev = device_get_binding(DT_LABEL(DT_INST(0, adi_adxl362)));
	if (dev == NULL) {
		printf("Device get binding device\n");
		return;
	}

	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
			printf("Trigger set error\n");
			return;
		}

		trig.type = SENSOR_TRIG_DATA_READY;
		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
			printf("Trigger set error\n");
		}
	}

	gpio_pin_set(leddev, PIN, false);


	

	while (true) {
		if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
			k_sem_take(&sem, K_FOREVER);
		} else {
			
			k_sleep(K_MSEC(1000));
			if (sensor_sample_fetch(dev) < 0) {
				printf("Sample fetch error\n");
				return;
			}
			
				
			
		}

		if (collect_data_flag && count < 300 ) {

			if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]) < 0) {
				printf("Channel get error\n");
				return;
			}

			if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]) < 0) {
				printf("Channel get error\n");
				return;
			}

			if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]) < 0) {
				printf("Channel get error\n");
				return;
			}

			printf("x: %.1f, y: %.1f, z: %.1f (m/s^2)\n",
				sensor_value_to_double(&accel[0]),
				sensor_value_to_double(&accel[1]),
				sensor_value_to_double(&accel[2]));
				count++;
				gpio_pin_set(leddev, PIN, true);
		}
		else if (collect_data_flag && (count >= 300) ) {
				collect_data_flag = false;
				gpio_pin_set(leddev, PIN, false);
		}
			
	}
}
