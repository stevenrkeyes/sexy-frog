#ifndef LED_PATTERN_H
#define LED_PATTERN_H

#include <Arduino.h>

// LED states
enum LEDState {
  LED_STATE_RED,
  LED_STATE_GREEN,
  LED_STATE_BLUE,
  LED_STATE_OFF
};

// Initialize LED pattern (call once in setup)
void initLedPattern();

// Run LED pattern state machine (call in loop)
void runLedPattern();

#endif // LED_PATTERN_H
