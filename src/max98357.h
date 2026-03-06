#ifndef MAX98357_H
#define MAX98357_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// MAX98357A is an I2S DAC+amp: it does not need register config,
// but we do need to drive the nRF52840 I2S peripheral correctly.
namespace max98357 {

// Initialize I2S TX for the MAX98357A.
// Pins are Arduino pin numbers (e.g. Dxx), not raw P0.xx.
// Currently supports 16-bit stereo frames, with mono duplicated to L/R.
bool begin(uint8_t bclk_pin, uint8_t lrclk_pin, uint8_t dout_pin);

// Stop/uninit I2S.
void end();

// Write mono 16-bit PCM samples (will be duplicated to L+R).
// Blocks until the chunk has been transmitted.
bool writeMono16(const int16_t* samples, size_t sample_count);

} // namespace max98357

#endif

