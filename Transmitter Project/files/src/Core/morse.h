/*
 * morse.h
 *
 * Created: 3/19/2018 3:16:02 PM
 *  Author: charl
 */ 


#ifndef MORSE_H_
#define MORSE_H_

#include "defs.h"

#define WPM_TO_MS_PER_DOT(s) (1200/(s)) 

/*
*/
typedef struct {
	uint8_t		pattern;
	uint8_t		lengthInSymbols;
	uint8_t		lengthInElements;
} MorseCharacter;

/**
Load a string to send by passing in a pointer to the string in the argument.
Call this function with a NULL argument at intervals of 1 element of time to generate Morse code.
Once loaded with a string each call to this function returns a BOOL indicating whether a CW carrier should be sent
 */
BOOL makeMorse(char* s, BOOL repeating, BOOL* finished);
uint16_t stringTimeRequiredToSend(char* str, uint16_t spd);

#endif /* MORSE_H_ */