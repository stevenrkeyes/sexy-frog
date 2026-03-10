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

// --- Double-buffered DMA streaming ---
// buf0, buf1: pointers to stereo frame buffers (uint32_t per frame, L|R 16-bit each).
// frame_count: number of stereo frames per buffer (same for both).
// Starts I2S with buf0; on each TXPTRUPD the driver switches to the other buffer
// and signals which buffer to refill via getRefillBufferIndex().
bool startStreaming(uint32_t* buf0, uint32_t* buf1, uint16_t frame_count);

// Stop streaming and disable I2S TX interrupt.
void stopStreaming();

// Returns 0 or 1 for the buffer that needs refill, or -1 if none.
// Main loop should fill that buffer (from SD) then call clearRefillRequest().
int getRefillBufferIndex();

void clearRefillRequest();

// Call after processing a refill: clears TXPTRUPD in main (nRF52 errata) and re-enables the interrupt.
// Not used when polling (see pollTxptrupd).
void clearTxptrupdAndReenableInterrupt();

// Poll for TXPTRUPD in main; if set, clear it, do buffer switch, set refill index. Returns true if refill needed.
// Use this instead of interrupts when the I2S interrupt is unreliable (e.g. nRF52 errata).
bool pollTxptrupd();

// Call when no more data: next buffer handed to I2S will be the last; after it plays we stop.
void requestStopAfterNextBuffer();

// True while I2S is running in streaming mode.
bool isStreaming();

} // namespace max98357

#endif

