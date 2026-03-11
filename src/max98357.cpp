#include "max98357.h"

// Use the nRF I2S HAL (inline, no extra link symbols needed).
#include <hal/nrf_i2s.h>

// I2S interrupt number for nRF52840 (from nrf52840.h)
#if !defined(I2S_IRQn)
#define I2S_IRQn 37
#endif

namespace max98357 {

// Keep buffers in Data RAM for EasyDMA.
static constexpr uint16_t kFramesPerChunk = 256; // 256 stereo frames per transfer
static uint32_t s_tx_buffer[kFramesPerChunk];

static bool s_inited = false;

// Double-buffer streaming state
static uint32_t* s_buf0 = nullptr;
static uint32_t* s_buf1 = nullptr;
static uint16_t s_frame_count = 0;
static volatile uint8_t s_next_index = 0;   // 0 or 1: which buffer to give next to I2S
static volatile int8_t s_refill_index = -1; // 0 or 1: which buffer needs refill; -1 = none
static volatile bool s_stop_after_next = false;
static volatile bool s_streaming = false;
static volatile bool s_first_txptrupd = true; // first TXPTRUPD after START: no buffer finished yet

extern "C" void I2S_IRQHandler(void) {
  if (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
    return;
  }
  // Do NOT clear the event here: on nRF52 it can hang/fail in the ISR. Disable the
  // interrupt so we return once; main will clear the event and re-enable the interrupt.
  nrf_i2s_int_disable(NRF_I2S, NRF_I2S_INT_TXPTRUPD_MASK);

  if (!s_streaming || !s_buf0 || !s_buf1) {
    return;
  }

  if (s_stop_after_next) {
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
    s_streaming = false;
    s_refill_index = -1;
    return;
  }

  // The buffer we gave last time is now in use. The other buffer just finished playing.
  uint8_t next;
  if (s_first_txptrupd) {
    s_first_txptrupd = false;
    next = 1;
    s_refill_index = -1; // no buffer finished yet
  } else {
    s_refill_index = (int8_t)(1 - s_next_index); // buffer that just finished needs refill
    next = 1 - s_next_index;
  }
  s_next_index = next;

  uint32_t* next_buf = (next == 0) ? s_buf0 : s_buf1;
  nrf_i2s_tx_buffer_set(NRF_I2S, next_buf);
}

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
  stopStreaming();
  nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
  nrf_i2s_disable(NRF_I2S);
  s_inited = false;
}

bool startStreaming(uint32_t* buf0, uint32_t* buf1, uint16_t frame_count) {
  if (!s_inited || !buf0 || !buf1 || frame_count == 0) {
    return false;
  }
  s_buf0 = buf0;
  s_buf1 = buf1;
  s_frame_count = frame_count;
  s_next_index = 0;
  s_refill_index = -1;
  s_stop_after_next = false;
  s_streaming = true;
  s_first_txptrupd = true;

  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_STOPPED);

  nrf_i2s_enable(NRF_I2S);

  nrf_i2s_transfer_set(NRF_I2S, frame_count, nullptr, buf0);

  // Do NOT enable the interrupt yet. Trigger START and handle the first TXPTRUPD in main.
  // On nRF52, clearing TXPTRUPD in the ISR can hang (errata); the first event fires immediately.
  nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);

  uint32_t t0 = millis();
  while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
    if ((millis() - t0) > 100) {
      nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
      nrf_i2s_disable(NRF_I2S);
      s_streaming = false;
      return false;
    }
  }
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);
  nrf_i2s_tx_buffer_set(NRF_I2S, buf1);
  s_next_index = 1;
  s_first_txptrupd = false;
  return true;
}

bool pollTxptrupd() {
  if (!s_streaming || !s_buf0 || !s_buf1) {
    return false;
  }
  if (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD)) {
    return false;
  }
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);

  if (s_stop_after_next) {
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
    s_streaming = false;
    s_refill_index = -1;
    return false;
  }

  uint8_t next;
  if (s_first_txptrupd) {
    s_first_txptrupd = false;
    next = 1;
    s_refill_index = -1;
  } else {
    s_refill_index = (int8_t)(1 - s_next_index); // buffer that just finished needs refill
    next = 1 - s_next_index;
  }
  s_next_index = next;
  uint32_t* next_buf = (next == 0) ? s_buf0 : s_buf1;
  nrf_i2s_tx_buffer_set(NRF_I2S, next_buf);
  return true;
}

void stopStreaming() {
  if (s_streaming) {
    NVIC_DisableIRQ((IRQn_Type)I2S_IRQn);
    nrf_i2s_int_disable(NRF_I2S, NRF_I2S_INT_TXPTRUPD_MASK);
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
    s_streaming = false;
  }
  if (s_inited && s_buf0) {
    uint32_t t = millis();
    while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_STOPPED) && (millis() - t < 200)) {
      // wait
    }
    nrf_i2s_disable(NRF_I2S);
  }
  s_buf0 = nullptr;
  s_buf1 = nullptr;
  s_refill_index = -1;
}

int getRefillBufferIndex() {
  return (int)s_refill_index;
}

void clearRefillRequest() {
  s_refill_index = -1;
}

void clearTxptrupdAndReenableInterrupt() {
  nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);
  nrf_i2s_int_enable(NRF_I2S, NRF_I2S_INT_TXPTRUPD_MASK);
}

void requestStopAfterNextBuffer() {
  s_stop_after_next = true;
}

bool isStreaming() {
  return s_streaming;
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
