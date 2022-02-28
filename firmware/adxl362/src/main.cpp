/*
 * Copyright (c) 2019 Brett Witherspoon
 *
 * SPDX-License-Identifier: Apache-2.0
 */


//#define LTE_ON // turn on LTE for the device


#include <zephyr.h>
#include <sys/printk.h>
#include <devicetree.h>
#include <drivers/gpio.h>

#include <device.h>
#include <drivers/sensor.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include <algorithm>

#ifdef LTE_ON
#include <string.h>
#include <stdlib.h>
#include <net/socket.h>
#include <modem/nrf_modem_lib.h>
#include <net/tls_credentials.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include <stdio.h>
#endif


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
K_SEM_DEFINE(data_ready,0,1);

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
	This timer starts the countdown to cancel 
*/
struct k_timer alert_countdown_timer;

// User has 15 seconds to cancel message
// shortened to 1.5 sec for testing
#define COUNTDOWN_INTERVAL_MSEC 1500

#define HTTPS_PORT 443

#define HTTPS_HOSTNAME "proud-wind-646.super-hokio.workers.dev"

#define HTTP_HEAD                                                              \
	"HEAD / HTTP/1.1\r\n"                                                  	\
	"Host: " HTTPS_HOSTNAME "\r\n"                                     	\
	"Accept: */*\r\n"														\
	"Connection: close\r\n\r\n"													



#define HTTP_HEAD_LEN (sizeof(HTTP_HEAD) - 1)

#define HTTP_HDR_END "\r\n\r\n"

#define RECV_BUF_SIZE 2048
#define TLS_SEC_TAG 42

//static const char send_buf[] = HTTP_HEAD;
static char recv_buf[RECV_BUF_SIZE];

// controls the sending of alerts
bool send_alert=false;

/* Set up variable buffer for sending messages */
char send_buf[500];

#define POST_TEMPLATE "POST / HTTP/1.1\r\n"\
		"Host: " HTTPS_HOSTNAME "\r\n"\
		"Connection: close\r\n"\
		"Content-Type: application/json\r\n"\
		"Content-length: %d\r\n\r\n"\
		"%s"

#define TEST_DATA "{\"type\":\"alert\"}\r\n\r\n"


/* Certificate for the website */
static const char cert[] = {
	#include "../cert/ISRGRootX1CA.pem"
};

BUILD_ASSERT(sizeof(cert) < KB(4), "Certificate too large");


#ifdef LTE_ON
/* Provision certificate to modem */
int cert_provision(void)
{
	int err;
	bool exists;
	int mismatch;

	/* It may be sufficient for you application to check whether the correct
	 * certificate is provisioned with a given tag directly using modem_key_mgmt_cmp().
	 * Here, for the sake of the completeness, we check that a certificate exists
	 * before comparing it with what we expect it to be.
	 */
	err = modem_key_mgmt_exists(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
	if (err) {
		printk("Failed to check for certificates err %d\n", err);
		return err;
	}

	if (exists) {
		mismatch = modem_key_mgmt_cmp(TLS_SEC_TAG,
					      MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
					      cert, strlen(cert));
		if (!mismatch) {
			printk("Certificate match\n");
			return 0;
		}

		printk("Certificate mismatch\n");
		err = modem_key_mgmt_delete(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
		if (err) {
			printk("Failed to delete existing certificate, err %d\n", err);
		}
	}

	printk("Provisioning certificate\n");

	/*  Provision certificate to the modem */
	err = modem_key_mgmt_write(TLS_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				   cert, sizeof(cert) - 1);
	if (err) {
		printk("Failed to provision certificate, err %d\n", err);
		return err;
	}

	return 0;
}

/* Setup TLS options on a given socket */
int tls_setup(int fd)
{
	int err;
	int verify;

	/* Security tag that we have provisioned the certificate with */
	const sec_tag_t tls_sec_tag[] = {
		TLS_SEC_TAG,
	};

#if defined(CONFIG_SAMPLE_TFM_MBEDTLS)
	err = tls_credential_add(tls_sec_tag[0], TLS_CREDENTIAL_CA_CERTIFICATE, cert, sizeof(cert));
	if (err) {
		return err;
	}
#endif

	/* Set up TLS peer verification */
	enum {
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	verify = REQUIRED;

	err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if (err) {
		printk("Failed to setup peer verification, err %d\n", errno);
		return err;
	}

	/* Associate the socket with the security tag
	 * we have provisioned the certificate with.
	 */
	err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
			 sizeof(tls_sec_tag));
	if (err) {
		printk("Failed to setup TLS sec tag, err %d\n", errno);
		return err;
	}

	err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME, HTTPS_HOSTNAME, sizeof(HTTPS_HOSTNAME) - 1);
	if (err) {
		printk("Failed to setup TLS hostname, err %d\n", errno);
		return err;
	}
	return 0;
}

/*
	Setup an LTE connection
*/

void setup_connection() {
		int err;
	int fd;
	char *p;
	int bytes;
	size_t off;
	struct addrinfo *res;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	int send_data_len;

	send_data_len = snprintf(send_buf,500,POST_TEMPLATE,15,TEST_DATA);

	printk("HTTPS client sample started\n\r");

	

#if !defined(CONFIG_SAMPLE_TFM_MBEDTLS)
	/* Provision certificates before connecting to the LTE network */
	err = cert_provision();
	if (err) {
		return;
	}
#endif

	printk("Waiting for network.. ");
	err = lte_lc_init_and_connect();
	if (err) {
		printk("Failed to connect to the LTE network, err %d\n", err);
		return;
	}
	printk("OK\n");


	err = getaddrinfo(HTTPS_HOSTNAME, NULL, &hints, &res);
	if (err) {
		printk("getaddrinfo() failed, err %d\n", errno);
		return;
	}

	((struct sockaddr_in *)res->ai_addr)->sin_port = htons(HTTPS_PORT);

	if (IS_ENABLED(CONFIG_SAMPLE_TFM_MBEDTLS)) {
		fd = socket(AF_INET, SOCK_STREAM | SOCK_NATIVE_TLS, IPPROTO_TLS_1_2);
	} else {
		fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
	}
	if (fd == -1) {
		printk("Failed to open socket!\n");
		goto clean_up;
	}

	/* Setup TLS socket options */
	err = tls_setup(fd);
	if (err) {
		goto clean_up;
	}
	

}

/*
	Send an alert to a waiting endpoint
*/

void send_alert() {
	printk("Connecting to %s\n", HTTPS_HOSTNAME);
	err = connect(fd, res->ai_addr, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect() failed, err: %d\n", errno);
		goto clean_up;
	}
		off = 0;

	do {
		bytes = send(fd, &send_buf[off],send_data_len - off, 0);
		if (bytes < 0) {
			printk("send() failed, err %d\n", errno);
			goto clean_up;
		}
		off += bytes;
	} while (off < send_data_len );

	printk("Sent %d bytes\n", off);

	off = 0;
	do {
		bytes = recv(fd, &recv_buf[off], RECV_BUF_SIZE - off, 0);
		if (bytes < 0) {
			printk("recv() failed, err %d\n", errno);
			goto clean_up;
		}
		off += bytes;
	} while (bytes != 0 /* peer closed connection */);

	printk("%s\r\n",recv_buf);

	printk("Received %d bytes\n", off);

	/* Print HTTP response */
	p = strstr(recv_buf, "\r\n");
	if (p) {
		off = p - recv_buf;
		recv_buf[off + 1] = '\0';
		printk("\n>\t %s\n\n", recv_buf);
	}

	printk("Finished, closing socket.\n");

clean_up:
	freeaddrinfo(res);
	(void)close(fd);

	lte_lc_power_off();
}


#endif

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

		// insert into first set
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
void send_alert_event(struct k_timer *timer_id){
	#ifdef LTE_ON
    int err = send_alert();
    if (err) {
        printk("Error in alert sending: %d\n", err);
    }
	#endif
	// flash to indicate that the message has been sent
	printk("Alert sent!\r\n");
	for (auto i=0; i<5; ++i) {
		gpio_pin_set(ledgreen, PIN1, true);
		k_sleep(K_MSEC(500));
		gpio_pin_set(ledgreen, PIN1, false);
		k_sleep(K_MSEC(500));
	}
	send_alert = false; // alert has been sent
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
		k_timer_start(&data_sampling_timer, K_MSEC(TIMER_INTERVAL_MSEC), K_FOREVER);

	} 

	if (send_alert) {
		printk("Alert cancelled");
		// cancel the alert
		send_alert = false;
		k_timer_stop(&alert_countdown_timer);
		gpio_pin_set(ledgreen, PIN1, false);
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
	// set up a timer to alert the lte
	k_timer_init(&alert_countdown_timer, send_alert_event, NULL);
	

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
					printk("%f",result.anomaly);
					
					if(result.anomaly>ANOMALY_LIMIT) {
						printk("Over Anomaly limit");
						gpio_pin_set(ledgreen, PIN1, true);
						send_alert = true;

						// wait for cancellation
						// send data to cloud
						#ifdef LTE_ON
						// only set it up here if 
						setup_lte_connection();

						#endif
						// set up a timer that will send out an alert if the button is not pressed
						printk("Starting timer\r\n");
						// start the one shot timer - cancel by pressing the multi-use button
						k_timer_start(&alert_countdown_timer, K_MSEC(COUNTDOWN_INTERVAL_MSEC),K_MSEC(COUNTDOWN_INTERVAL_MSEC));

					} else {
						printk("Ok\r\n");
						/*
						if(!send_alert) {
							gpio_pin_set(ledgreen, PIN1, false);

						}
						*/
						
					}
			#endif
					printk("]\n");
			
			// not necessary to reset the buffer because it is a queue with a counter to keep track when the next sample set is ready
			
		} else {
			if (collect_data_flag) {
				//printk("Progress: %u/%d\n",sample_counter,EI_CLASSIFIER_RAW_SAMPLE_COUNT);
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
