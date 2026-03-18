#include "device_list.h"
#include <string.h>
#include <stdio.h>

BleDeviceList::BleDeviceList() : m_count(0) {
  for (int i = 0; i < kMaxDevices; i++) {
    m_devices[i].inUse = false;
  }
}

void BleDeviceList::copyString(char* dst, size_t dstLen, const String& src) {
  if (dstLen == 0) return;
  size_t n = src.length();
  if (n >= dstLen) n = dstLen - 1;
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

int BleDeviceList::findIndexByAddress(const char* address) {
  for (int i = 0; i < kMaxDevices; i++) {
    if (m_devices[i].inUse && strcmp(m_devices[i].address, address) == 0) {
      return i;
    }
  }
  return -1;
}

int BleDeviceList::findEmptySlot() {
  for (int i = 0; i < kMaxDevices; i++) {
    if (!m_devices[i].inUse) return i;
  }
  return -1;
}

void BleDeviceList::addOrUpdate(BLEDevice& peripheral) {
  String addrStr = peripheral.address();
  if (addrStr.length() == 0) return;
  const char* addr = addrStr.c_str();

  int idx = findIndexByAddress(addr);
  if (idx < 0) {
    idx = findEmptySlot();
    if (idx < 0) return; // list full
    m_devices[idx].inUse = true;
    strncpy(m_devices[idx].address, addr, sizeof(m_devices[idx].address) - 1);
    m_devices[idx].address[sizeof(m_devices[idx].address) - 1] = '\0';
    m_count++;
  }

  BleDeviceInfo& d = m_devices[idx];
  d.rssi = peripheral.rssi();
  copyString(d.localName, sizeof(d.localName), peripheral.localName());
  copyString(d.deviceName, sizeof(d.deviceName), peripheral.deviceName());
  d.appearance = (uint16_t)peripheral.appearance();
  d.lastSeenMs = millis();

  d.companyId = 0xFFFF;
  d.mfgSubtype = 0xFF;
  if (peripheral.hasManufacturerData() && peripheral.manufacturerDataLength() >= 2) {
    int len = peripheral.manufacturerDataLength();
    uint8_t buf[32];
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    peripheral.manufacturerData(buf, len);
    d.companyId = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    if (len >= 3) {
      d.mfgSubtype = buf[2];
    }
  }
}

void BleDeviceList::pruneStale(unsigned long maxAgeMs) {
  unsigned long now = millis();
  for (int i = 0; i < kMaxDevices; i++) {
    if (!m_devices[i].inUse) continue;
    if ((now - m_devices[i].lastSeenMs) > maxAgeMs) {
      m_devices[i].inUse = false;
      m_count--;
    }
  }
}

// Well-known (companyId, manufacturer data subtype at offset 2) pairs.
// Apple subtypes from continuity / proximity advertising (reverse-engineered, common in field).
static const struct {
  uint16_t companyId;
  uint8_t subtype;
  const char* name;
} kKnownSubtypes[] = {
    {0x004C, 0x01, "AirDrop"},
    {0x004C, 0x02, "iBeacon"},
    {0x004C, 0x03, "AirPrint"},
    {0x004C, 0x05, "AirPlay"},
    {0x004C, 0x06, "AppleTV"},
    {0x004C, 0x07, "AirPods/Beats"},
    {0x004C, 0x09, "AirPlay src"},
    {0x004C, 0x0A, "AirPlay tgt"},
    {0x004C, 0x0C, "Handoff"},
    {0x004C, 0x0D, "Typing"},
    {0x004C, 0x0F, "Nearby"},
    {0x004C, 0x10, "NearbyAction"},
    {0x004C, 0x12, "FindMy/Prox"},
    {0x004C, 0x14, "HomeKit"},
    {0x004C, 0x16, "AutoUnlock"},
    // Google Fast Pair (company 0xFE2C): first payload byte after CI often used as type
    {0xFE2C, 0x00, "FastPair"},
    {0xFE2C, 0x02, "FastPair md"},
};

const char* BleDeviceList::subtypeNameOrHex(uint16_t companyId, uint8_t subtype, char* hexBuf,
                                            size_t hexBufLen) const {
  if (subtype == 0xFF) {
    return "-";
  }
  for (size_t i = 0; i < sizeof(kKnownSubtypes) / sizeof(kKnownSubtypes[0]); i++) {
    if (kKnownSubtypes[i].companyId == companyId && kKnownSubtypes[i].subtype == subtype) {
      return kKnownSubtypes[i].name;
    }
  }
  if (hexBuf && hexBufLen >= 5) {
    snprintf(hexBuf, hexBufLen, "0x%02X", subtype);
    return hexBuf;
  }
  return "?";
}

// Well-known Bluetooth SIG company identifiers (subset).
const char* BleDeviceList::companyNameOrId(uint16_t companyId) const {
  switch (companyId) {
    case 0x004C: return "Apple";
    case 0x00E0: return "Google";
    case 0x0006: return "Microsoft";
    case 0x0059: return "Nordic";
    case 0x0075: return "Garmin";
    case 0x0087: return "Bose";
    case 0x00E4: return "Samsung";
    case 0x0153: return "Amazon";
    case 0xFE2C: return "Google FP";
    case 0xFFFF: return "-";
    default:     return nullptr; // print as hex
  }
}

void BleDeviceList::printTable() {
  // Build list of in-use indices and sort by RSSI descending (strongest first).
  int indices[kMaxDevices];
  int n = 0;
  for (int i = 0; i < kMaxDevices; i++) {
    if (m_devices[i].inUse) indices[n++] = i;
  }
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (m_devices[indices[j]].rssi > m_devices[indices[i]].rssi) {
        int t = indices[i];
        indices[i] = indices[j];
        indices[j] = t;
      }
    }
  }

  const int colRssi = 6, colAddr = 18, colName = 16;

  Serial.println();
  Serial.println("============ BLE Device List (by RSSI) ============");
  Serial.print("RSSI   Address            Local Name      Device Name     App   Company     Subtype");
  Serial.println();
  Serial.println("-----  ----------------- --------------- --------------- ----  ----------  ----------");

  for (int i = 0; i < n; i++) {
    const BleDeviceInfo& d = m_devices[indices[i]];

    char rssiBuf[8];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d", (int)d.rssi);
    Serial.print(rssiBuf);
    for (int k = (int)strlen(rssiBuf); k < colRssi; k++) Serial.print(' ');
    Serial.print("  ");
    Serial.print(d.address);
    for (int k = (int)strlen(d.address); k < colAddr; k++) Serial.print(' ');
    Serial.print("  ");

    const char* ln = d.localName[0] ? d.localName : "-";
    Serial.print(ln);
    for (int k = (int)strlen(ln); k < colName; k++) Serial.print(' ');
    Serial.print("  ");
    const char* dn = d.deviceName[0] ? d.deviceName : "-";
    Serial.print(dn);
    for (int k = (int)strlen(dn); k < colName; k++) Serial.print(' ');
    Serial.print("  0x");
    if (d.appearance < 0x10) Serial.print("0");
    Serial.print(d.appearance, HEX);
    Serial.print("  ");

    const char* company = companyNameOrId(d.companyId);
    if (company) {
      Serial.print(company);
      for (int k = (int)strlen(company); k < 10; k++) Serial.print(' ');
    } else {
      char cbuf[8];
      snprintf(cbuf, sizeof(cbuf), "0x%04X", d.companyId);
      Serial.print(cbuf);
      for (int k = (int)strlen(cbuf); k < 10; k++) Serial.print(' ');
    }
    Serial.print("  ");

    char subHex[8];
    const char* sub = subtypeNameOrHex(d.companyId, d.mfgSubtype, subHex, sizeof(subHex));
    Serial.println(sub);
  }
  Serial.print("Total: ");
  Serial.print(n);
  Serial.println(" device(s)");
  Serial.println("==================================================");
  Serial.println();
}
