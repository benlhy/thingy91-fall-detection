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
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include <algorithm>


/* The devicetree node identifier for the nodelabel in the dts file. */
#define SW0_NODE DT_ALIAS(sw0)

#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;


/* The devicetree node identifier for the nodelabel in the dts file. */
// RED
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

/* The devicetree node identifier for the nodelabel in the dts file. */
// GREEN
#define LED1_NODE DT_ALIAS(led1)

#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
#define LED1	DT_GPIO_LABEL(LED1_NODE, gpios)
#define PIN1	DT_GPIO_PIN(LED1_NODE, gpios)
#define FLAGS1	DT_GPIO_FLAGS(LED1_NODE, gpios)
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0	""
#define PIN	0
#define FLAGS	0
#endif

#define TIMER_INTERVAL_MSEC 60
#define TOTAL_DATA_POINTS 3000

#define PRINT_ACCEL_DATA false
#define ANOMALY_LIMIT 1.4



K_SEM_DEFINE(sem, 0, 1);

struct k_timer data_sampling_timer;

/*
	Controls start and stop of the data sampling timer
*/

bool collect_data_flag; 
int count;

const struct device *dev;
struct sensor_value accel[3];

const struct device *ledred;
const struct device *ledgreen;


/*
	Features are determined by x,y,z,v,x,y,z,v...
*/
int features_index = 0; // used to keep track of the first fill of the data buffer
int sample_counter = 0; // counter to indicate when a new frame is ready

static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

/*
	Sample the IMU
	@returns 0 if okay, otherwise returns 1

*/

static int imu_sample(void) {

	if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]) < 0) {
		printf("Channel get error\n");
		return 1;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]) < 0) {
		printf("Channel get error\n");
		return 1;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]) < 0) {
		printf("Channel get error\n");
		return 1;
	}

	float x = sensor_value_to_double(&accel[0]);
	float y = sensor_value_to_double(&accel[1]);
	float z = sensor_value_to_double(&accel[2]);

	float v = sqrt(pow(x,2.0)+pow(y,2.0)+pow(z,2.0));

	if (PRINT_ACCEL_DATA) {
			printf("x: %.1f, y: %.1f, z: %.1f, v: %.1f (m/s^2)\n",
		x,
		y,
		z,
		v);

	}


	
	// implement a queue like feature
	if (features_index==EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {

		// insert into first element
		features[0] = x;
		features[1] = y;
		features[2] = z;
		features[3] = v;

		// utilize cpp rotate
		std::rotate(&features[0],&features[4],&features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE]);



		// shift all elements in the array to the right by 4 (removing one sample)
		/*
		for(int i=0;i<last_set;++i) {
			features[i*4] =	features[i*4+4];
			features[i*4+1] =  features[i*4+5];
			features[i*4+2] =  features[i*4+6];
			features[i*4+3] =  features[i*4+7];
		}
		*/


	} else {

		// fill up the features buffer
		features[features_index] = x;
		features[features_index+1] = y;
		features[features_index+2] = z;
		features[features_index+3] = v;
		features_index = features_index+4;
	}

	// the sample counter will keep track of each new sample set

	if (sample_counter==EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
		sample_counter = 0;
	} else {
		sample_counter = sample_counter + 1;
	}

	count++;

	
	return 0;

}

void imu_sample_event(struct k_timer *timer_id){
    int err = imu_sample();
    if (err) {
        printk("Error in adc sampling: %d\n", err);
    }
}

/*
	Interrupt handler for the button
*/

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	// use button as toggle
	if (collect_data_flag) {
		// stop collecting data
		collect_data_flag = false;
		k_timer_stop(&data_sampling_timer);
		gpio_pin_set(ledred, PIN, false);

	} else {
		collect_data_flag = true;	// set flag to collect data
		gpio_pin_set(ledred, PIN, true);
		// start timer for data sampling
		k_timer_start(&data_sampling_timer, K_MSEC(TIMER_INTERVAL_MSEC), K_MSEC(TIMER_INTERVAL_MSEC));

	}
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
	case SENSOR_TRIG_FREEFALL:
		printf("Freefall trigger\n");
		break;
	default:
		printf("Unknown trigger\n");
	}
}

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}

void main(void)
{
	// set up a timer to sample the imu at a set interval
	k_timer_init(&data_sampling_timer, imu_sample_event, NULL);
	

	// set up LED
	int ret;
	bool led_on = true;
	
	ledred = device_get_binding(LED0);
	if (ledred==NULL) {
		printk("No device found!");
		return;
	}
	ret = gpio_pin_configure(ledred,PIN, GPIO_OUTPUT_ACTIVE | FLAGS);

	if (ret<0 ){
		return;
	}
	gpio_pin_set(ledred, PIN, false);

	ledgreen = device_get_binding(LED1);
	if (ledgreen==NULL) {
		printk("No device found!");
		return;
	}
	ret = gpio_pin_configure(ledgreen,PIN1, GPIO_OUTPUT_ACTIVE | FLAGS1);

	if (ret<0 ){
		return;
	}

	gpio_pin_set(ledgreen, PIN1, true);


	
	collect_data_flag = false;
	count = 0; // reset count

	// set up Buttons
	
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


	// set up IMU

	dev = device_get_binding(DT_LABEL(DT_INST(0, adi_adxl362)));
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

		trig.type = SENSOR_TRIG_FREEFALL;
		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
			printf("Trigger set error\n");
		}
	}

	gpio_pin_set(ledgreen, PIN1, false);	// setup complete
	
	// setup impulse result
	ei_impulse_result_t result = { 0 };

	
	k_sleep(K_MSEC(1000));



	while (true) {

		// the features are stored into flash, and we don't want to load everything into RAM
		if (sample_counter==EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
			// new frame is ready
			printk("Features array full, starting computation.\n");

			signal_t features_signal;
			features_signal.total_length = sizeof(features) / sizeof(features[0]);
			features_signal.get_data = &raw_feature_get_data;

			// invoke the impulse, turn off the debug
			EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
			printk("run_classifier returned: %d\n", res);

			if (res != 0) printk("Error detected!");

			printk("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
				result.timing.dsp, result.timing.classification, result.timing.anomaly);

			// print the predictions
			printk("[");
			for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
				ei_printf_float(result.classification[ix].value);
			#if EI_CLASSIFIER_HAS_ANOMALY == 1
						printk(", ");
			#else
						if (ix != EI_CLASSIFIER_LABEL_COUNT - 1) {
							printk(", ");
						}
			#endif
					}
			#if EI_CLASSIFIER_HAS_ANOMALY == 1
					ei_printf_float(result.anomaly);
					if(result.anomaly>ANOMALY_LIMIT) {
						gpio_pin_set(ledgreen, PIN1, true);
					} else {
						gpio_pin_set(ledgreen, PIN1, false);
					}
			#endif
					printk("]\n");
			
			// not necessary to reset the buffer because it is a queue with a counter to keep track when the next sample set is ready
			
		} else {
			if (collect_data_flag) {
				printk("Progress: %u/%d\n",
				sample_counter,EI_CLASSIFIER_RAW_SAMPLE_COUNT);
			// continue to collect data
			}
			
		}


		// Standard main loop

		if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
			k_sem_take(&sem, K_FOREVER);
		} else {
			
			k_sleep(K_MSEC(1000));
			if (sensor_sample_fetch(dev) < 0) {
				printf("Sample fetch error\n");
				return;
			}	
		}

		/*

		// if the collect_data_flag has been set 

		if (collect_data_flag && count < TOTAL_DATA_POINTS ) {
			// continue to collect data
			gpio_pin_set(ledred, PIN, true);
		}
		else if (collect_data_flag && (count >= TOTAL_DATA_POINTS) ) {
			// stop the periodic timer
			k_timer_stop(&data_sampling_timer);
			collect_data_flag = false;
			gpio_pin_set(ledred, PIN, false);
		}
		*/
			
	}
}
