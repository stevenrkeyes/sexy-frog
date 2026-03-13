#ifndef DEVICE_LIST_H
#define DEVICE_LIST_H

#include <Arduino.h>
#include <ArduinoBLE.h>

// Info for one BLE device (indexed by address, display sorted by RSSI).
struct BleDeviceInfo {
  char address[18];      // "xx:xx:xx:xx:xx:xx"
  int8_t rssi;
  char localName[33];
  char deviceName[33];
  uint16_t appearance;
  uint16_t companyId;    // from manufacturer data (first 2 bytes LE), 0xFFFF if none
  unsigned long lastSeenMs;
  bool inUse;
};

// Device list: map by address (linear search) + printable order by RSSI.
class BleDeviceList {
 public:
  static const int kMaxDevices = 100;

  BleDeviceList();

  // Add or update device from a scanned peripheral.
  void addOrUpdate(BLEDevice& peripheral);

  // Remove devices not seen in the last maxAgeMs milliseconds.
  void pruneStale(unsigned long maxAgeMs);

  // Print table: RSSI, address, local name, device name, appearance, company (or company ID).
  void printTable();

 private:
  BleDeviceInfo m_devices[kMaxDevices];
  int m_count;

  int findIndexByAddress(const char* address);
  int findEmptySlot();
  void copyString(char* dst, size_t dstLen, const String& src);
  const char* companyNameOrId(uint16_t companyId) const;
};

#endif // DEVICE_LIST_H
