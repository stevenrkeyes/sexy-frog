#ifndef CAP_TOUCH_H
#define CAP_TOUCH_H

#include <Arduino.h>
#include <stdint.h>

// ADCTouch-style capacitive sensing (SAADC). Call begin(), then update() in loop;
// use rose()/fell() for touch start/end edges.
//
// Touch is detected when (mean - raw) > kSigma * stddev (reading drops when touched).
// Only non-touching readings are stored in the window; mean/stddev describe the idle baseline.

class CapTouch {
 public:
  static const uint16_t kMaxWindow = 32;

  // pin: Arduino pin 4 (A4/D4) or 5 (A5/D5) only.
  // windowSize: number of past raw readings to average (<= kMaxWindow).
  // kSigma: touch if (mean - raw) > kSigma * stddev (min stddev clamped to avoid noise).
  CapTouch(uint8_t pin, uint16_t windowSize = 32, float kSigma = 2.5f);

  bool begin();

  // One measurement cycle; updates rolling stats, touch state, and edge flags.
  void update();

  // True for this cycle only: touch started / ended since last update().
  bool rose() const { return rose_; }
  bool fell() const { return fell_; }

  bool touching() const { return touching_; }
  int16_t raw() const { return lastRaw_; }
  float mean() const { return mean_; }
  float stddev() const { return stddev_; }

  // For the next `updates` calls to update(), rose() and fell() are forced false (edges suppressed).
  void beginCooldown(uint16_t updates);

 private:
  uint8_t pin_;
  uint16_t windowSize_;
  float kSigma_;

  int16_t buf_[kMaxWindow];
  uint16_t idx_;   // next write index
  uint16_t n_;     // number of valid samples (<= windowSize_)

  int16_t lastRaw_;
  float mean_;
  float stddev_;

  bool touching_;
  bool rose_;
  bool fell_;
  uint16_t cooldownRemaining_;

  int16_t measureRaw_();
  void computeStats_();
};

#endif
