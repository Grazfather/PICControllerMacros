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

#define CONTROLLER_BUTTONS 0b00100000

typedef enum {
	IDLE,
	DEBOUNCE,
	WAIT,
	RECORDING,
	SAVING,
	PLAYBACK,
} state_t;

typedef union{
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
 *	1000000/256 = 3906.25 interrupts/second.
 *	Prescale down to 1/4 -> 976.56 per second
 *	If debounce in 20ms ~= 20 interrupts
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
	IOCbits.IOC3 = 1; // Interrupt on GPIO3 changing.

	state = IDLE;

	// Set up timer0
	OPTION_REGbits.T0CS = 0; // Base on oscillator/4
	OPTION_REGbits.PSA = 0; // Use prescaler
	// 1:4 / 1000000/256/4 = 976.56 interrupts/sec
	OPTION_REGbits.PS = 0b001;
	INTCONbits.TMR0IE = 1; // Enable
	ANSEL = 0; // Disable all analog pins so we can use them as GPIOs

	// Enable interrupts
	ei();
	TRISIO = CONTROLLER_BUTTONS; // Spare GPIOs for debugging
	while(1)
	{
		// TODO: Allow uC to sleep

		// Debug code to see current state
		// TODO: Add DEBUG macros
		switch (state) {
			case IDLE:
				sGPIO.bits.GP0 = 0;
				sGPIO.bits.GP1 = 0;
				sGPIO.bits.GP2 = 0;
				sGPIO.bits.GP4 = 0;
				break;
			case DEBOUNCE:
				sGPIO.bits.GP0 = 1;
				sGPIO.bits.GP1 = 0;
				sGPIO.bits.GP2 = 0;
				sGPIO.bits.GP4 = 0;
				break;
			case WAIT:
				sGPIO.bits.GP0 = 0;
				sGPIO.bits.GP1 = 1;
				sGPIO.bits.GP2 = 0;
				sGPIO.bits.GP4 = 0;
				break;
			case RECORDING:
				sGPIO.bits.GP0 = 0;
				sGPIO.bits.GP1 = 0;
				sGPIO.bits.GP2 = 1;
				sGPIO.bits.GP4 = 0;
				break;
			case PLAYBACK:
				sGPIO.bits.GP0 = 0;
				sGPIO.bits.GP1 = 0;
				sGPIO.bits.GP2 = 0;
				sGPIO.bits.GP4 = 1;
				break;
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
		sGPIO.reg = GPIO; // Read state of inputs.
		switch(state) {
			case IDLE:
				break;
			case DEBOUNCE:
				if (sGPIO.bits.GP3 == 0) { // Pressed
					if (++debounce >= DEBOUNCE_CYCLES) {
						debounce = 0;
						TRISIO = CONTROLLER_BUTTONS;
						state = WAIT;
					}
				} else {
					debounce = 0;
					TRISIO = CONTROLLER_BUTTONS;
					state = IDLE;
				}
				break;
			case WAIT:
				if (sGPIO.bits.GP3 == 0) { // Pressed
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
				if (sGPIO.bits.GP3 == 0) {
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
					}
					// If the GPIO is supposed to be PRESSED, set it to output,
					// Otherwise set the pin as input/high impedence.
					TRISIO = recording[index] ^ PRESSED_MASK;
					// Set the GPIOs that were pressed to PRESSED.
					sGPIO.reg = recording[index] & CONTROLLER_BUTTONS;
					GPIO = sGPIO.reg;
					index++;
				}
				break;
			default:
				break;
		}
	}

	if (INTCONbits.GPIF) {
		if (state == IDLE)
		{
			// Start counting for debounce;
			state = DEBOUNCE;
			debounce = 0;
		}
	}

	// Clear interrupt flags
	INTCONbits.TMR0IF = 0;
	INTCONbits.GPIF = 0;

	return;
}
