/*
 *	devilbadge.c
 *	C for a DevilBadge
 */

#define F_CPU 16000000L
#define BAUD 9600
#define BRC ((F_CPU/16/BAUD) - 1)
#define TX_BUFFER_SIZE 128
#define RX_BUFFER_SIZE 128
#define MAX_DUTY_CYCLE 64.0			// MUST BE POWER OF 2
#define MIN_DUTY_CYCLE 0.5			// MUST BE POWER OF 2
#define PENTACLE_DELAY 50
#define PENTACLE_ACCELERATION 1.125
#define MAX_LIGHT 127
#define INITIAL_BADGE_STATE locked	// locked, lamb, soul, solved

#include <avr/io.h>
#include <avr/power.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdlib.h>
#include <string.h>

double dutyCycle;
char adc_val[10];

char serialBuffer[TX_BUFFER_SIZE];
char rxBuffer[RX_BUFFER_SIZE];
uint8_t serialReadPos = 0;
uint8_t serialWritePos = 0;
uint8_t rxReadPos = 0;
uint8_t rxWritePos = 0;
uint8_t lastKnownState = 0;

char getChar();
char peekChar();
void appendSerial(char c);
void serialWrite(char c[]);

void setupADC();
void startConversion();
void stopConversion();
void pentacleLoop();
void pentacleFadeIn();
void pentacleFadeOut();
void turnEyesOff();
void turnEyesOn();

enum EyeState {
	on,
	off
};

enum EyeState nextEyeState = off;
enum EyeState currentEyeState = off;

enum BadgeState {
	locked,
	lamb,
	soul,
	solved
};

enum BadgeState badgeState = INITIAL_BADGE_STATE;

int main(void) {
	cli();

	clock_prescale_set(clock_div_1);	// Stop dividing the clock frequency by 8

	DDRD = (1<<PORTD6) | (1<<PORTD4);
	PORTB |= (1<<PORTB1);				// Set internal pull-up resistor for lamb pin

	// For Timer 0 -- PWM
	TCCR0A = (1<<COM0A1) | (1<<WGM00) | (1<<WGM01);

	// For Timer 1 -- Serial messages
	TCCR1B = (1<<WGM12);
	OCR1A = 62500;
	TIMSK1 = (1<<OCIE1A);	// Setup interrupt

	UBRR0H = (BRC>>8);
	UBRR0L = BRC;

	UCSR0B = (1<<TXEN0) | (1<<TXCIE0) | (1<<RXEN0) | (1<<RXCIE0);
	UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);

	setupADC();

	sei();

	// Start timers
	//TCCR0B = (1<<CS00); // TODO Did it work?
	TCCR1B |= (1<<CS12) | (1<<CS10); // Set prescalar and start counting

	// Get last state from EEPROM
	lastKnownState = eeprom_read_byte((uint8_t*)0x00);
	if (lastKnownState > 1 && lastKnownState < 4) {
		badgeState = lastKnownState;
	}

	while(1) {

		_delay_ms(10);
		if (badgeState == solved) {
			// Finish line
			serialWrite("Entering Pentacle Loop.");
			TCCR0B = (1<<CS00); // TODO Did it work?
			pentacleLoop();

			// Moorse Code easter egg
			//serialWrite("Entering Moorse Mode!\n\r");
		}
		_delay_ms(10);
		// Sacrificed lamb; sell soul?
		if (PINB & (1<<PORTB1)) {
			stopConversion();
			_delay_ms(10);
			//serialWrite("Yummy.\n\r");
			badgeState = soul;
			turnEyesOn();
			eeprom_write_byte((void*)0x00, badgeState);
		}
		_delay_ms(10);
		// Soul sold; on to bigger and brighter things
		if (badgeState == soul && (getChar() == 'y' || getChar() == 'Y')) {
			serialWrite("There is no savior nor destroyer. No paradise nor damnation. There is only this Flesh, and beyond it there is darkness. Always. Have a wonderful SatanCon!\n\r");
			_delay_ms(50);
			cli();
			badgeState = solved;
			eeprom_write_byte((void*)0x00, badgeState);
		}
	}
}

ISR(TIMER1_COMPA_vect) {
	// locked
	if (badgeState == locked) {
		_delay_ms(10);
		serialWrite("Unseen goes darkness in the Light, for it is hidden. Unseen goes light in the Darkness, for it is destroyed.\n\r");
	}
	// lamb
	if (badgeState == lamb) {
		_delay_ms(10);
		serialWrite("Let the blood of His flock flow like rivers.\n\r");
		_delay_ms(10);
		serialWrite("Drink me into thee and bear witness, for the bleeding of the Shepherd is come.\n\r");
	}
	// soul
	if (badgeState == soul) {
		_delay_ms(10);
		serialWrite("Burnt offering are a savor, lo' I make no commandment. Only in earth and flesh can one be freed, and now in covenant, thou give of thyself freely.\n\r");
		_delay_ms(10);
		serialWrite("Wouldst thou like to live deliciously? I will guide thy hand. y/n\n\r");
	}
	// solved
	// nothing to do
}

ISR(USART_RX_vect) {
	rxBuffer[rxWritePos] = UDR0;

	rxWritePos++;

	if (rxWritePos >= RX_BUFFER_SIZE) {
		rxWritePos = 0;
	}
}

ISR(USART_TX_vect) {
	if (serialReadPos != serialWritePos) {
		UDR0 = serialBuffer[serialReadPos];
		serialReadPos++;

		if (serialReadPos >= TX_BUFFER_SIZE) {
			serialReadPos++;
		}
	}
}

ISR(ADC_vect) {
	if ((badgeState != solved) && (!(PINB & (1<<PORTB1)))) {

		if (ADC > MAX_LIGHT) {
			nextEyeState = off;
			if (currentEyeState != nextEyeState) {
				currentEyeState = nextEyeState;
				turnEyesOff();
				badgeState = locked;
			}
		} else {
			nextEyeState = on;
			if (currentEyeState != nextEyeState) {
				currentEyeState = nextEyeState;
				turnEyesOn();
				badgeState = lamb;
			}
		}
	startConversion();

	}
}

void setupADC() {
	ADMUX = (1<<REFS0);
	ADCSRA = (1<<ADEN) | (1<<ADIE) | (1<<ADPS0) | (1<<ADPS1) | (1<<ADPS2);
	DIDR0 = (1<<ADC0D);		// Disable digital input to analog pin

	startConversion();
}

void startConversion() {
	ADCSRA |= (1<<ADSC);
}

void stopConversion() {
	ADCSRA &= ~(1<<ADSC);
}

void pentacleLoop() {
	cli();
	turnEyesOn();
	while(1) {
		pentacleFadeIn();
		_delay_ms(PENTACLE_DELAY);
		pentacleFadeOut();
		/*
		// Return to enter Moorse Mode
		if (!(PINB & (1<<PORTB1))) {
			return;
		}
		*/
	}
}

void pentacleFadeIn() {
	cli();

	if (dutyCycle != MIN_DUTY_CYCLE)
		dutyCycle = MIN_DUTY_CYCLE;

	while (dutyCycle < MAX_DUTY_CYCLE) {
		_delay_ms(PENTACLE_DELAY);
		dutyCycle *= PENTACLE_ACCELERATION;
		OCR0A = (dutyCycle/100.0)*255;
	}

	if (dutyCycle != MAX_DUTY_CYCLE)
		dutyCycle = MAX_DUTY_CYCLE;

}

void pentacleFadeOut() {
	cli();
	if (dutyCycle != MAX_DUTY_CYCLE)
		dutyCycle = MAX_DUTY_CYCLE;

	while (dutyCycle > MIN_DUTY_CYCLE) {
		_delay_ms(PENTACLE_DELAY);
		dutyCycle /= PENTACLE_ACCELERATION;
		OCR0A = (dutyCycle/100.0)*255;
	}

	if (dutyCycle != MIN_DUTY_CYCLE)
		dutyCycle = MIN_DUTY_CYCLE;
}

void turnEyesOff() {
	PORTD &= ~(1<<PORTD4);
}

void turnEyesOn() {
	PORTD |= (1<<PORTD4);
}

void appendSerial(char c) {
	serialBuffer[serialWritePos] = c;
	serialWritePos++;

	if (serialWritePos >= TX_BUFFER_SIZE) {
		serialWritePos = 0;
	}
}

void serialWrite(char c[]) {
	for (uint8_t i = 0; i < strlen(c); i++) {
		appendSerial(c[i]);
	}

	if (UCSR0A & (1<<UDRE0)) {
		UDR0 = 0;
	}
}

char peekChar() {
	char ret = '\0';

	if (rxReadPos != rxWritePos) {
		ret = rxBuffer[rxReadPos];
	}

	return ret;
}

char getChar() {
	char ret = '\0';

	if (rxReadPos != rxWritePos) {
		ret = rxBuffer[rxReadPos];

		rxReadPos++;

		if (rxReadPos >= RX_BUFFER_SIZE) {
			rxReadPos = 0;
		}
	}

	return ret;
}
