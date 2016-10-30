#ifndef __ROTARY_ENCODER_H__
#define __ROTARY_ENCODER_H__

struct rotary_encoder_platform_data {
	unsigned int steps;
	unsigned int axis;
	unsigned int gpio_a;
	unsigned int gpio_b;
	unsigned int gpio_c;
	unsigned int inverted_a;
	unsigned int inverted_b;
	unsigned int key;
	/* key debounce interval in milli-second */
	unsigned int debounce_ms;
	bool active_low;
	bool relative_axis;
	bool rollover;
	bool half_period;
};

#endif /* __ROTARY_ENCODER_H__ */
