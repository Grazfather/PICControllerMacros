/*
 * Controller button macro recorder/playback
 * Grazfather
 *
 * PIC12f683
 *
 * Pin assignments:
 *	GP0: Controller button 1
 *	GP1: Controller button 2
 *	GP2: Controller button 3
 *	GP3: Play/record button
 *	GP4: Controller button 4
 *	GP5: Controller button 5
 */

#include <stdio.h>
#include <stdlib.h>
#include <xc.h>

#pragma config BOREN=OFF, FOSC=INTOSCIO, CPD=OFF, CP=OFF, MCLRE=OFF, PWRTE=OFF, WDTE=OFF

#define DEBOUNCE_CYCLES 20
#define RECORD_INTERVAL 10

// Will we read high or low when a button is pressed?
#define PRESSED 0
#define PRESSED_MASK ((unsigned char)0 - PRESSED)
//                           xx54_210
#define CONTROLLER_BUTTONS 0b00110111

typedef enum {
	IDLE,
	DEBOUNCE,
	WAIT,
	RECORDING,
	SAVING,
	PLAYBACK,
} state_t;

typedef union {
	unsigned char reg;
	GPIObits_t bits;
} shadowGPIO_t;

volatile unsigned char recording[80];
volatile unsigned char index = 0;
volatile unsigned char length = 0;
volatile state_t state;
volatile shadowGPIO_t sGPIO;

/*
 * Timer0: 8 bit. pre scaler
 *	4MHz/4 clocks/tick = 1MHz increments
 *	1000000 ticks/second / 256 ticks/interrupt= 3906.25 interrupts/second.
 *	Prescale down to 1/4 -> 976.56 interrupts/second
 *	If debounce is 20ms ~= 20 interrupts
 *	GIE set to enable interrupts
 *	T0IE flag set to enable
 *	T0IF flag must be cleared.
 */

int main() {
	// We want to minimize reads and writes to GPIO to avoid
	// the read-modify-write issue, so we will have a shadow copy
	sGPIO.reg = 0b00000000;
	GPIO = sGPIO.reg; // All GPIOs off
	TRISIO = 0b00111111; // All buttons are inputs
	OPTION_REGbits.nGPPU = 0; // Enable weak pullups
	WPU = 0b00111111; // Weak pullups on all GPIOs

	IOCbits.IOC3 = 1; // Interrupt on GPIO3 changing.

	state = IDLE;

	// Set up timer0
	OPTION_REGbits.T0CS = 0; // Base on oscillator/4
	OPTION_REGbits.PSA = 0; // Use prescaler
	OPTION_REGbits.PS = 0b001; // Prescaler: 1:4
	INTCONbits.TMR0IE = 1; // Enable
	ANSEL = 0; // Disable all analog pins so we can use them as GPIOs
	CMCON0 = 0b111; // Clear the comparator for GP1 to work

	// Enable interrupts
	INTCONbits.GPIE = 1;
	INTCONbits.GIE = 1;
	TRISIO = CONTROLLER_BUTTONS; // Spare GPIOs for debugging
	while(1)
	{
		// TODO: Allow uC to sleep

		// Debug code to see current state
		// TODO: Add DEBUG macros
		switch (state) {
			case IDLE:
				//sGPIO.bits.GP0 = PRESSED;
				//sGPIO.bits.GP1 = !PRESSED;
				//sGPIO.bits.GP2 = PRESSED;
				SLEEP();
				break;
			//case DEBOUNCE:
				//sGPIO.bits.GP0 = PRESSED;
				//sGPIO.bits.GP1 = !PRESSED;
				//sGPIO.bits.GP2 = !PRESSED;
				//break;
			//case WAIT:
				//sGPIO.bits.GP0 = !PRESSED;
				//sGPIO.bits.GP1 = PRESSED;
				//sGPIO.bits.GP2 = !PRESSED;
				//break;
			//case RECORDING:
				//sGPIO.bits.GP0 = !PRESSED;
				//sGPIO.bits.GP1 = !PRESSED;
				//sGPIO.bits.GP2 = PRESSED;
				//break;
			//case PLAYBACK:
				//sGPIO.bits.GP0 = PRESSED;
				//sGPIO.bits.GP1 = PRESSED;
				//sGPIO.bits.GP2 = PRESSED;
				//break;
			case SAVING: // Unused
			default:
				break;
		}
		GPIO = sGPIO.reg;
	}

	return (EXIT_SUCCESS);
}

void interrupt isr(void)
{
	static unsigned char debounce = 0;

	if (INTCONbits.TMR0IF) { // Timer0 overflow
		switch(state) {
			case IDLE:
				break;
			case DEBOUNCE:
				sGPIO.reg = GPIO; // Read state of inputs.
				if (sGPIO.bits.GP3 == PRESSED) {
					if (++debounce >= DEBOUNCE_CYCLES) {
						TRISIO = CONTROLLER_BUTTONS;
						state = WAIT;
					}
				} else {
					TRISIO = CONTROLLER_BUTTONS;
					state = IDLE;
				}
				break;
			case WAIT:
				sGPIO.reg = GPIO; // Read state of inputs.
				if (sGPIO.bits.GP3 == PRESSED) {
					// Check every button for a press. If anything has been
					// pressed, record it and jump to the recording state.
					// Otherwise, do nothing.

					// If any button is pressed
					if (~(sGPIO.reg ^ PRESSED_MASK) & CONTROLLER_BUTTONS) {
						debounce = 0;
						index = 0;
						state = RECORDING;
						// Record it.
						recording[index++] = sGPIO.reg;
					}
				} else {
					debounce = 0;
					index = 0;
					state = PLAYBACK;
				}
				break;
			case RECORDING:
				// If we are still holding 'record'
				sGPIO.reg = GPIO; // Read state of inputs.
				if (sGPIO.bits.GP3 == PRESSED) {
					if (++debounce == RECORD_INTERVAL) {
						debounce = 0;
						recording[index++] = sGPIO.reg;

						if (index >= sizeof(recording)) {
							// If we record for too long, just stop recording.
							length = index;
							TRISIO = CONTROLLER_BUTTONS;
							state = IDLE;
						}
					}
				} else {
					// Done recording
					length = index;
					TRISIO = CONTROLLER_BUTTONS;
					state = IDLE;
					// state = SAVING;
				}
				break;
			case SAVING:
				break;
			case PLAYBACK:
				if (++debounce == RECORD_INTERVAL) {
					debounce = 0;

					if (index >= length) { // Done playback
						TRISIO = CONTROLLER_BUTTONS;
						state = IDLE;
					} else {
						// If the GPIO is supposed to be PRESSED, set it to output,
						// Otherwise set the pin as input/high impedence.
						TRISIO = recording[index] & CONTROLLER_BUTTONS;
						// Set the GPIOs that were pressed to PRESSED.
						sGPIO.reg = recording[index] & CONTROLLER_BUTTONS;
						GPIO = sGPIO.reg;
						index++;
					}
				}
				break;
			default:
				break;
		}

	// Clear interrupt flags
	INTCONbits.TMR0IF = 0;
	}

	if (INTCONbits.GPIF) {
		if (state == IDLE)
		{
			// Start counting for debounce;
			state = DEBOUNCE;
			debounce = 0;
		}
	INTCONbits.GPIF = 0;
	}

	return;
}
