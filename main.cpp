#include <pico/stdlib.h>
#include <stdio.h>

#include "brain-common/brain-common.h"
#include "brain-ui/led.h"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

using brain::ui::Led;

const uint LED_PINS[] = {
	BRAIN_LED_1, BRAIN_LED_2, BRAIN_LED_3, BRAIN_LED_4, BRAIN_LED_5, BRAIN_LED_6};

int main() {
	stdio_init_all();

	// Initialize LEDs
	brain::ui::Led leds[6] = {
		brain::ui::Led(LED_PINS[0]), brain::ui::Led(LED_PINS[1]),
		brain::ui::Led(LED_PINS[2]), brain::ui::Led(LED_PINS[3]),
		brain::ui::Led(LED_PINS[4]), brain::ui::Led(LED_PINS[5])};

	for (uint i = 0; i < 6; i++) {
		leds[i].init();
	}

	printf("brain-cv-tuner started\n");

	// Simple LED blink pattern
	uint led_index = 0;
	while (true) {
		leds[led_index].toggle();
		led_index = (led_index + 1) % 6;
		sleep_ms(100);
	}

	return 0;
}
