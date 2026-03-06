#include "max98357.h"

// Use the nRF I2S HAL (inline, no extra link symbols needed).
#include <hal/nrf_i2s.h>

namespace max98357 {

// Keep buffers in Data RAM for EasyDMA.
static constexpr uint16_t kFramesPerChunk = 256; // 256 stereo frames per transfer
static uint32_t s_tx_buffer[kFramesPerChunk];

static bool s_inited = false;

bool begin(uint8_t bclk_pin, uint8_t lrclk_pin, uint8_t dout_pin) {
  if (s_inited) {
    return true;
  }

  // Configure pins.
  // Note: These are raw GPIO pin numbers in this HAL. For this Arduino-mbed core,
  // Arduino pin numbers map 1:1 to the underlying GPIO for the XIAO pinout.
  nrf_i2s_pins_set(NRF_I2S,
                   bclk_pin,
                   lrclk_pin,
                   NRF_I2S_PIN_NOT_CONNECTED,
                   dout_pin,
                   NRF_I2S_PIN_NOT_CONNECTED);

  // Configure I2S for 16-bit stereo I2S format.
  // Clocking chosen for ~44.1kHz:
  // LRCK = (32MHz / 23) / 32 = 43478 Hz
  const bool ok = nrf_i2s_configure(NRF_I2S,
                                    NRF_I2S_MODE_MASTER,
                                    NRF_I2S_FORMAT_I2S,
                                    NRF_I2S_ALIGN_LEFT,
                                    NRF_I2S_SWIDTH_16BIT,
                                    NRF_I2S_CHANNELS_STEREO,
                                    NRF_I2S_MCK_32MDIV23,
                                    NRF_I2S_RATIO_32X);
  if (!ok) {
    return false;
  }

  s_inited = true;
  return true;
}

void end() {
  if (!s_inited) {
    return;
  }

  nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
  nrf_i2s_disable(NRF_I2S);
  s_inited = false;
}

static bool writeFramesBlocking(uint32_t const* frames, uint16_t frame_count) {
  if (!s_inited || frame_count == 0) {
    return false;
  }

  // Enable + set transfer.
  nrf_i2s_enable(NRF_I2S);
  nrf_i2s_transfer_set(NRF_I2S, frame_count, nullptr, frames);

  // TXPTRUPD fires:
  // - once immediately after START (internal buffer gets initial pointer)
  // - again when the peripheral is ready for the next buffer (after current buffer duration)
  // We use the 2nd TXPTRUPD as "buffer consumed" marker.
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_STOPPED);

  nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);

  uint32_t start_ms = millis();

  // Wait for 1st TXPTRUPD (should be quick).
  while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
    if ((millis() - start_ms) > 100) {
      nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
      nrf_i2s_disable(NRF_I2S);
      return false;
    }
  }
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);

  // Wait for 2nd TXPTRUPD (roughly after buffer played).
  while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
    if ((millis() - start_ms) > 2000) {
      break;
    }
  }

  nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
  // Wait for STOPPED to ensure clean stop.
  uint32_t stop_ms = millis();
  while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_STOPPED)) {
    if ((millis() - stop_ms) > 200) {
      break;
    }
  }
  nrf_i2s_disable(NRF_I2S);
  return true;
}

bool writeMono16(const int16_t* samples, size_t sample_count) {
  if (!samples || sample_count == 0) {
    return true;
  }

  size_t idx = 0;
  while (idx < sample_count) {
    const size_t remaining = sample_count - idx;
    const uint16_t frames = (remaining > kFramesPerChunk) ? kFramesPerChunk : (uint16_t)remaining;

    // Pack mono samples into I2S stereo frames (L=sample, R=sample).
    for (uint16_t i = 0; i < frames; i++) {
      const uint16_t s = (uint16_t)samples[idx + i];
      s_tx_buffer[i] = ((uint32_t)s << 16) | (uint32_t)s;
    }
    // Zero-pad rest of the buffer (not strictly necessary when frame_count < chunk)
    for (uint16_t i = frames; i < kFramesPerChunk; i++) {
      s_tx_buffer[i] = 0;
    }

    if (!writeFramesBlocking(s_tx_buffer, frames)) {
      return false;
    }

    idx += frames;
  }

  return true;
}

} // namespace max98357

