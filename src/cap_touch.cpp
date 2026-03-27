#include "cap_touch.h"
#include <hal/nrf_saadc.h>
#include <cmath>

static const nrf_saadc_input_t kInputVddhDiv5 = (nrf_saadc_input_t)0x0DUL;

static nrf_saadc_input_t pinToPositiveInput(uint8_t pin) {
  if (pin == 4) return NRF_SAADC_INPUT_AIN2;
  if (pin == 5) return NRF_SAADC_INPUT_AIN3;
  return NRF_SAADC_INPUT_DISABLED;
}

static void channelConfigCommon(nrf_saadc_channel_config_t* cfg) {
  cfg->resistor_p = NRF_SAADC_RESISTOR_DISABLED;
  cfg->resistor_n = NRF_SAADC_RESISTOR_DISABLED;
  cfg->gain = NRF_SAADC_GAIN1_4;
  cfg->reference = NRF_SAADC_REFERENCE_VDD4;
  cfg->acq_time = NRF_SAADC_ACQTIME_20US;
  cfg->mode = NRF_SAADC_MODE_SINGLE_ENDED;
  cfg->burst = NRF_SAADC_BURST_DISABLED;
  cfg->pin_n = NRF_SAADC_INPUT_DISABLED;
}

static bool waitEvent(nrf_saadc_event_t ev, uint32_t spinMax) {
  while (spinMax-- > 0) {
    if (nrf_saadc_event_check(ev)) {
      return true;
    }
  }
  return false;
}

static int16_t sampleOnce(void) {
  static nrf_saadc_value_t buf[1];
  nrf_saadc_resolution_set(NRF_SAADC_RESOLUTION_12BIT);
  nrf_saadc_oversample_set(NRF_SAADC_OVERSAMPLE_DISABLED);
  nrf_saadc_buffer_init(buf, 1);

  nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);
  nrf_saadc_event_clear(NRF_SAADC_EVENT_END);
  nrf_saadc_event_clear(NRF_SAADC_EVENT_RESULTDONE);

  nrf_saadc_task_trigger(NRF_SAADC_TASK_START);
  if (!waitEvent(NRF_SAADC_EVENT_STARTED, 100000)) {
    return 0;
  }
  nrf_saadc_event_clear(NRF_SAADC_EVENT_STARTED);

  nrf_saadc_task_trigger(NRF_SAADC_TASK_SAMPLE);
  if (!waitEvent(NRF_SAADC_EVENT_RESULTDONE, 100000)) {
    return 0;
  }
  nrf_saadc_event_clear(NRF_SAADC_EVENT_RESULTDONE);

  return buf[0];
}

CapTouch::CapTouch(uint8_t pin, uint16_t windowSize, float kSigma)
    : pin_(pin),
      windowSize_(windowSize > kMaxWindow ? kMaxWindow : (windowSize < 2 ? 2 : windowSize)),
      kSigma_(kSigma),
      idx_(0),
      n_(0),
      lastRaw_(0),
      mean_(0),
      stddev_(0),
      touching_(false),
      rose_(false),
      fell_(false),
      cooldownRemaining_(0) {}

void CapTouch::beginCooldown(uint16_t updates) {
  cooldownRemaining_ = updates;
}

bool CapTouch::begin() {
  if (pinToPositiveInput(pin_) == NRF_SAADC_INPUT_DISABLED) {
    return false;
  }
  pinMode(pin_, INPUT_PULLUP);
  idx_ = 0;
  n_ = 0;
  if (!nrf_saadc_enable_check()) {
    nrf_saadc_enable();
  }
  return true;
}

int16_t CapTouch::measureRaw_() {
  nrf_saadc_input_t ain = pinToPositiveInput(pin_);
  if (ain == NRF_SAADC_INPUT_DISABLED) return 0;

  if (!nrf_saadc_enable_check()) {
    nrf_saadc_enable();
  }

  static const uint8_t kCh = 7;
  nrf_saadc_channel_config_t cfg;
  channelConfigCommon(&cfg);

  cfg.pin_p = kInputVddhDiv5;
  nrf_saadc_channel_init(kCh, &cfg);
  (void)sampleOnce();

  pinMode(pin_, INPUT);

  cfg.pin_p = ain;
  nrf_saadc_channel_init(kCh, &cfg);
  int16_t value = sampleOnce();

  pinMode(pin_, INPUT_PULLUP);

  return value;
}

void CapTouch::computeStats_() {
  if (n_ == 0) {
    mean_ = 0;
    stddev_ = 0;
    return;
  }
  double sum = 0;
  for (uint16_t i = 0; i < n_; i++) {
    int16_t v = (n_ < windowSize_) ? buf_[i] : buf_[(idx_ + i) % windowSize_];
    sum += v;
  }
  mean_ = (float)(sum / (double)n_);

  double varSum = 0;
  for (uint16_t i = 0; i < n_; i++) {
    int16_t v = (n_ < windowSize_) ? buf_[i] : buf_[(idx_ + i) % windowSize_];
    double d = (double)v - (double)mean_;
    varSum += d * d;
  }
  stddev_ = (float)sqrt(varSum / (double)n_);
}

void CapTouch::update() {
  rose_ = false;
  fell_ = false;

  lastRaw_ = measureRaw_();

  computeStats_();

  const float minSigma = 4.0f;
  float sigma = stddev_;
  if (sigma < minSigma) {
    sigma = minSigma;
  }
  const float threshold = kSigma_ * sigma;

  bool isTouching = false;
  if (n_ == 0) {
    isTouching = false;
  } else {
    isTouching = ((float)lastRaw_ - (float)mean_) > threshold;
  }

  // Only store non-touching readings in the window; mean/stddev describe the idle baseline.
  if (!isTouching) {
    buf_[idx_] = lastRaw_;
    idx_ = (idx_ + 1) % windowSize_;
    if (n_ < windowSize_) {
      n_++;
    }
    computeStats_();
  }

  bool wasTouching = touching_;
  touching_ = isTouching;

  if (!wasTouching && touching_) {
    rose_ = true;
  }
  if (wasTouching && !touching_) {
    fell_ = true;
  }

  if (cooldownRemaining_ > 0) {
    rose_ = false;
    fell_ = false;
    cooldownRemaining_--;
  }
}
