/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/pwm.h>


/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0	DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN	DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS	DT_GPIO_FLAGS(LED0_NODE, gpios)
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0	""
#define PIN	0
#define FLAGS	0
#endif

#define PWM_LED1_NODE	DT_ALIAS(pwmled)

#if DT_NODE_HAS_STATUS(PWM_LED1_NODE, okay)
#define PWM_CTLR	DT_PWMS_CTLR(PWM_LED1_NODE)
#define PWM_CHANNEL	DT_PWMS_CHANNEL(PWM_LED1_NODE)
#define PWM_FLAGS	DT_PWMS_FLAGS(PWM_LED1_NODE)
#else
#error "Unsupported board: pwmled devicetree alias is not defined"
#define PWM_CTLR	DT_INVALID_NODE
#define PWM_CHANNEL	0
#define PWM_FLAGS	0
#endif


// #if DT_NODE_HAS_STATUS(DT_ALIAS(pwmsound), okay)
// #define PWM_DRIVER  DT_PWMS_LABEL(DT_ALIAS(pwmsound))
// #define PWM_CHANNEL DT_PWMS_CHANNEL(DT_ALIAS(pwmsound))
// #else
// #error "Choose a supported PWM driver"
// #endif



#define MIN_PERIOD_USEC	(USEC_PER_SEC / 64U)
#define MAX_PERIOD_USEC	USEC_PER_SEC

void main(void)
{
	const struct device *dev;
	int ret;
	bool led_on = true;
	printk("Hello World! %s\n", CONFIG_BOARD);

	dev = device_get_binding(LED0);
	if (dev==NULL) {
		printk("No device found!");
		return;
	}
	ret = gpio_pin_configure(dev,PIN, GPIO_OUTPUT_ACTIVE | FLAGS);

	if (ret<0 ){
		return;
	}

	const struct device *pwm;
	uint32_t max_period;
	uint32_t period;
	uint8_t dir = 0U;
	pwm = DEVICE_DT_GET(PWM_CTLR);
	if (!device_is_ready(pwm)) {
		printk("PWM device is not ready!");
		return;
	} else {
		printk("PWM device is ready");
	}

	/*
	 * In case the default MAX_PERIOD_USEC value cannot be set for
	 * some PWM hardware, decrease its value until it can.
	 *
	 * Keep its value at least MIN_PERIOD_USEC * 4 to make sure
	 * the sample changes frequency at least once.
	 */
	//printk("Calibrating for channel %d...\n", PWM_CHANNEL);
	max_period = MAX_PERIOD_USEC;
	while (pwm_pin_set_usec(pwm, PWM_CHANNEL,
	 			max_period, max_period / 2U, PWM_FLAGS)) {
		max_period /= 2U;
		if (max_period < (4U * MIN_PERIOD_USEC)) {
			printk("Error: PWM device "
			       "does not support a period at least %u\n",
			       4U * MIN_PERIOD_USEC);
			return;
		}
	}
	printk("Done calibrating; maximum/minimum periods %u/%u usec\n",
	       max_period, MIN_PERIOD_USEC);
	period = max_period;

	ret = pwm_pin_set_usec(pwm,PWM_CHANNEL,period,period/2U,PWM_FLAGS);


	while(1) {
		ret = pwm_pin_set_usec(pwm,PWM_CHANNEL,period,period/2U,PWM_FLAGS);

		if (ret) {
			printk("Error, failed to set pulse width");
			return;
		}

		period = dir?(period*2U): (period/2U);
		if (period > max_period) {
			period = max_period / 2U;
			dir = 0U;
		} else if (period < MIN_PERIOD_USEC) {
			period = MIN_PERIOD_USEC * 2U;
			dir = 1Ul;
		}


		gpio_pin_set(dev, PIN, led_on);
		led_on = !led_on;
		k_msleep(1000);
		printk("Looping!\n");
	}
}
