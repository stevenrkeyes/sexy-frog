/*
 * nRF52840 BLE Sense XIAO - BLE Scanner
 *
 * Maintains a device list (by address), sorted by RSSI for display.
 * Prunes devices not seen in 60s, every 10s. Prints table: RSSI, address,
 * local name, device name, appearance, company.
 */

#include <Arduino.h>
#include <ArduinoBLE.h>
#include "led_pattern.h"
#include "audio_player.h"
#include "device_list.h"
#include "cap_touch.h"

static BleDeviceList s_deviceList;
static unsigned long s_lastPruneMs = 0;
static const unsigned long kPruneIntervalMs = 10000;  // every 10 seconds
static const unsigned long kStaleAgeMs = 60000;      // remove if not seen in 60s

// Cap touch on A4/D4 (P0.04). Use A5: CapTouch touch(A5, 32, 2.5f).
static CapTouch s_touch(A4, 32, 5.0f);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  delay(1000);
  Serial.println("nRF52840 BLE Sense XIAO - BLE Scanner");
  Serial.println("Initializing BLE...");
  Serial.println();

  initLedPattern();

  if (initAudioPlayer()) {
    listSDFiles();
    Serial.println("Playing startup sound...");
    playSoundFile("FROG-C~4.WAV");
    s_touch.beginCooldown(5);
  } else {
    Serial.println("Audio initialization failed - continuing without audio");
  }

  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }

  Serial.println("BLE initialized");
  Serial.println("Starting scan...");
  Serial.println("==========================================");
  Serial.println();

  BLE.scan();
  s_lastPruneMs = millis();

  if (s_touch.begin()) {
    Serial.println("Cap touch started");
  } else {
    Serial.println("Cap touch init failed");
  }
  s_touch.update();
}

void loop() {
  runLedPattern();

  BLEDevice peripheral = BLE.available();

  if (peripheral) {
    s_deviceList.addOrUpdate(peripheral);
  }

  unsigned long now = millis();
  if ((now - s_lastPruneMs) >= kPruneIntervalMs) {
    s_lastPruneMs = now;
    s_deviceList.pruneStale(kStaleAgeMs);
    s_deviceList.printTable();
  }

  s_touch.update();

  if (s_touch.rose()) {
    playSoundFile("FROG-C~4.WAV");
  }

  delay(100);
}
