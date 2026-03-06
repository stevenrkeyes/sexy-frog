#include "led_pattern.h"

// Track LED state
static LEDState currentLedState = LED_STATE_OFF;
static unsigned long lastLedChange = 0;
static const unsigned long LED_INTERVAL = 500;

void initLedPattern() {
  // Initialize LEDs
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  // Logic is inverted, so set all LEDs to high to turn them off
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

void runLedPattern() {
  // Cycle LED through states: RED -> GREEN -> BLUE -> OFF -> repeat
  unsigned long currentTime = millis();
  if (currentTime - lastLedChange >= LED_INTERVAL) {
    // Turn off current LED (inverted logic)
    switch (currentLedState) {
      case LED_STATE_RED:
        digitalWrite(LED_RED, HIGH);
        break;
      case LED_STATE_GREEN:
        digitalWrite(LED_GREEN, HIGH);
        break;
      case LED_STATE_BLUE:
        digitalWrite(LED_BLUE, HIGH);
        break;
      case LED_STATE_OFF:
        // Already off, nothing to do
        break;
    }
    
    // Advance to next state
    currentLedState = (LEDState)((currentLedState + 1) % 4);
    
    // Turn on new LED (inverted logic)
    switch (currentLedState) {
      case LED_STATE_RED:
        digitalWrite(LED_RED, LOW);
        break;
      case LED_STATE_GREEN:
        digitalWrite(LED_GREEN, LOW);
        break;
      case LED_STATE_BLUE:
        digitalWrite(LED_BLUE, LOW);
        break;
      case LED_STATE_OFF:
        // All LEDs already off
        break;
    }
    
    lastLedChange = currentTime;
  }
}
