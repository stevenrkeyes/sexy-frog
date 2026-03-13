/*
 * nRF52840 BLE Sense XIAO - BLE Scanner
 * 
 * This program:
 * - Scans for BLE devices
 * - Prints device information (address, name, RSSI, services, etc.)
 * - Blinks LED to show activity
 * 
 * Hardware:
 * - Seeed XIAO nRF52840 Sense
 */

#include <Arduino.h>
#include <ArduinoBLE.h>
#include "led_pattern.h"
#include "audio_player.h"

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Wait for serial port to connect
  }
  
  delay(1000);
  Serial.println("nRF52840 BLE Sense XIAO - BLE Scanner");
  Serial.println("Initializing BLE...");
  Serial.println();
  
  // Initialize LED pattern
  initLedPattern();

  // Initialize and play audio at startup
  if (initAudioPlayer()) {
    listSDFiles();
    Serial.println("Playing startup sound...");
    playSoundFile("NORM-F~1.WAV");
  } else {
    Serial.println("Audio initialization failed - continuing without audio");
  }
  
  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }
  
  Serial.println("BLE initialized");
  Serial.println("Starting scan...");
  Serial.println("==========================================");
  Serial.println();
  
  // Start scanning for peripherals
  BLE.scan();
}

void loop() {
  // Run LED pattern state machine
  runLedPattern();
  
  // Check if a peripheral is available
  BLEDevice peripheral = BLE.available();
  
  if (peripheral) {
    // Print separator
    Serial.println("------------------------------------------");
    
    // MAC Address (BLE Address)
    Serial.print("Address: ");
    Serial.println(peripheral.address());
    
    // Local Name
    Serial.print("Local Name: ");
    if (peripheral.localName().length() > 0) {
      Serial.println(peripheral.localName());
    } else {
      Serial.println("(No local name)");
    }
    
    // Device Name
    Serial.print("Device Name: ");
    if (peripheral.deviceName().length() > 0) {
      Serial.println(peripheral.deviceName());
    } else {
      Serial.println("(No device name)");
    }

    // Advertised Service UUIDs
    Serial.print("Advertised Service UUIDs: ");
    int serviceCount = peripheral.advertisedServiceUuidCount();
    if (serviceCount > 0) {
      Serial.println();
      for (int i = 0; i < serviceCount; i++) {
        Serial.print("  - ");
        Serial.println(peripheral.advertisedServiceUuid(i));
      }
    } else {
      Serial.println("(None)");
    }
    
    // RSSI (Signal Strength)
    Serial.print("RSSI: ");
    Serial.print(peripheral.rssi());
    Serial.println(" dBm");
    
    // Manufacturer Data
    Serial.print("Manufacturer Data: ");
    if (peripheral.hasManufacturerData()) {
      int dataLength = peripheral.manufacturerDataLength();
      uint8_t data[dataLength];
      int copied = peripheral.manufacturerData(data, dataLength);
      Serial.print("Length: ");
      Serial.print(copied);
      Serial.print(", Data: ");
      for (int i = 0; i < copied; i++) {
        Serial.print("0x");
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        if (i < copied - 1) Serial.print(" ");
      }
      Serial.println();
    } else {
      Serial.println("(None)");
    }
    
    // Appearance
    Serial.print("Appearance: ");
    Serial.print("0x");
    Serial.println(peripheral.appearance(), HEX);
    
    Serial.println();
  }
  
  delay(10);
}
