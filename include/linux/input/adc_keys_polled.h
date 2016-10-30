#ifndef __ADC_KEYS_H
#define __ADC_KEYS_H

struct adc_keys_platform_data {
	int debounce_interval;
	
	struct gpio_keys_button *buttons;
	int nbuttons;
	unsigned int poll_interval;	/* polling interval in msecs -
					   for polling driver only */
	unsigned int rep:1;		/* enable input subsystem auto repeat */
	int (*enable)(struct device *dev);
	void (*disable)(struct device *dev);
	const char *name;		/* input device name */
};

#endif
