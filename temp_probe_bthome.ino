// =============================================================================
// temp_probe_bthome.ino
//
// DS18B20 / DS18S20 temperature probe -> BTHome v2 Bluetooth advertisement.
//
// Nothing connects to this device. It reads the probe every READ_PERIOD_MS and
// broadcasts the result in a BTHome v2 service-data advertisement (UUID 0xFCD2).
// The Pi's BLE bridge already decodes that format and publishes it as
//
//     entgen/ble/<MAC12>/state   {"fields":{"temperature_c":4.19,...},...}
//
// so the hub, and every phone listening to it, hear this probe exactly the way
// they hear the Shelly door sensor.
//
// Advertisement layout (13 bytes of data plus the name, inside the 31-byte limit):
//   02 01 06            flags
//   09 16 D2 FC         length 9, service data, UUID 0xFCD2 (little endian)
//   40                  device info: BTHome v2, unencrypted, regular broadcast
//   00 <id>             object 0x00: packet id (changes with every reading)
//   02 <lo> <hi>        object 0x02: temperature, sint16, 0.01 degC steps
//   <n> 09 <name>       complete local name (in the broadcast: see advertiseTemperature)
// BTHome requires objects in ascending id order; 0x00 then 0x02 satisfies it.
//
// Differences from the original test sketch, and why:
//   * it WAITS for the conversion (about 750 ms) before reading the result;
//     the old sketch read the previous conversion's value, so the first reading
//     after power-up was the chip's reset value of 85 degC;
//   * the scratchpad CRC is checked; a noisy line no longer produces a reading;
//   * DS18S20 (family 0x10) and DS18B20 (family 0x28) are decoded correctly;
//   * the sensor is found once, not searched for on every read.
//
// BOARD NOTE: needs a board supported by ArduinoBLE. ONEWIRE_PIN and
// PARASITE_POWER below must match your wiring.
// =============================================================================

#include <OneWire.h>
#include <ArduinoBLE.h>

// -- Configuration ------------------------------------------------------------
#define ONEWIRE_PIN     18
#define PARASITE_POWER  1                 // 1: probe powered from the data line (2-wire)
                                          // 0: probe has its own VDD connection (3-wire)
const char*         BLE_NAME       = "EntGen-Temp";
const unsigned long READ_PERIOD_MS = 5000;   // one reading and one new packet id
const unsigned long CONVERT_MS     = 800;    // 12-bit conversion takes up to 750 ms
const int           ADV_INTERVAL   = 800;    // units of 0.625 ms -> 500 ms, so each
                                             // reading is sent about ten times
const float         MIN_VALID_C    = -55.0f; // DS18x20 working range
const float         MAX_VALID_C    = 125.0f;

OneWire ds(ONEWIRE_PIN);

byte          probeAddr[8];
bool          probeFound   = false;
bool          haveGood     = false;      // at least one accepted reading since boot
bool          converting   = false;
unsigned long nextAt       = 0;          // when to start the next conversion
unsigned long readyAt      = 0;          // when the running conversion is done
uint8_t       packetId     = 0;

// -- Probe --------------------------------------------------------------------

bool findProbe() {
  ds.reset_search();
  byte addr[8];
  while (ds.search(addr)) {
    if (OneWire::crc8(addr, 7) != addr[7]) continue;            // address CRC bad
    if (addr[0] != 0x10 && addr[0] != 0x28 && addr[0] != 0x22) continue;   // not a DS18x20
    memcpy(probeAddr, addr, 8);
    ds.reset_search();
    return true;
  }
  ds.reset_search();
  return false;
}

void startConversion() {
  ds.reset();
  ds.select(probeAddr);
  ds.write(0x44, PARASITE_POWER);        // CONVERT T; with parasite power the line is
                                         // held high for the whole conversion
}

// Reads the scratchpad and returns degrees C, or false on a bad read.
bool readTempC(float &outC) {
  byte data[9];
  if (!ds.reset()) return false;         // nothing answered the reset pulse
  ds.select(probeAddr);
  ds.write(0xBE);                        // READ SCRATCHPAD
  for (int i = 0; i < 9; i++) data[i] = ds.read();
  if (OneWire::crc8(data, 8) != data[8]) return false;

  int16_t raw = (int16_t)((data[1] << 8) | data[0]);
  if (probeAddr[0] == 0x10) {            // DS18S20: 9-bit, extended with the count registers
    raw = raw << 3;
    if (data[7] == 0x10) raw = (raw & 0xFFF0) + 12 - data[6];
  } else {                               // DS18B20 / DS1822: resolution is configurable
    byte cfg = data[4] & 0x60;
    if (cfg == 0x00)      raw &= ~7;     //  9 bit
    else if (cfg == 0x20) raw &= ~3;     // 10 bit
    else if (cfg == 0x40) raw &= ~1;     // 11 bit
  }
  float c = (float)raw / 16.0f;

  // 85.00 is what the chip holds before its first conversion. Accept it only
  // once a genuine reading has been seen (a real 85 degC is possible, a reset
  // value after power-up is not).
  if (!haveGood && c > 84.99f && c < 85.01f) return false;
  if (c < MIN_VALID_C || c > MAX_VALID_C) return false;

  outC = c;
  return true;
}

// -- BLE ----------------------------------------------------------------------

void advertiseTemperature(float tempC) {
  int16_t t = (int16_t)lroundf(tempC * 100.0f);    // object 0x02: 0.01 degC per step

  // The name goes in the broadcast itself. A non-connectable advertisement has
  // no scan response, so a name set there would never be sent and the hub would
  // see only the MAC address. 13 bytes of data + 2 of name header + the name must
  // fit in 31, which allows a name of up to 16 characters.
  uint8_t nameLen = (uint8_t)strlen(BLE_NAME);
  if (nameLen > 16) nameLen = 16;

  uint8_t adv[31];
  uint8_t n = 0;
  adv[n++] = 0x02; adv[n++] = 0x01; adv[n++] = 0x06;                 // flags
  adv[n++] = 0x09; adv[n++] = 0x16; adv[n++] = 0xD2; adv[n++] = 0xFC; // service data, UUID 0xFCD2
  adv[n++] = 0x40;                                                   // BTHome v2, unencrypted
  adv[n++] = 0x00; adv[n++] = packetId;                              // object 0x00: packet id
  adv[n++] = 0x02; adv[n++] = (uint8_t)(t & 0xFF);                   // object 0x02: temperature
  adv[n++] = (uint8_t)((t >> 8) & 0xFF);
  adv[n++] = nameLen + 1; adv[n++] = 0x09;                           // complete local name
  memcpy(adv + n, BLE_NAME, nameLen);
  n += nameLen;

  BLE.stopAdvertise();
  BLEAdvertisingData advData;
  advData.setRawData(adv, n);
  BLE.setAdvertisingData(advData);
  BLE.advertise();
}

// -- Arduino ------------------------------------------------------------------

void setup() {
  Serial.begin(9600);
  { unsigned long t0 = millis(); while (!Serial && millis() - t0 < 3000); }   // never wait forever for USB

  if (!BLE.begin()) {
    Serial.println("BLE failed to start.");
    while (1);
  }
  BLE.setConnectable(false);                       // broadcast only: nothing can connect
  BLE.setAdvertisingInterval(ADV_INTERVAL);

  probeFound = findProbe();
  Serial.println(probeFound ? "Probe found." : "No DS18x20 probe found; will keep looking.");
  nextAt = millis();
}

void loop() {
  BLE.poll();

  if (!probeFound) {
    if ((long)(millis() - nextAt) >= 0) {
      probeFound = findProbe();
      nextAt = millis() + READ_PERIOD_MS;
      if (probeFound) Serial.println("Probe found.");
    }
    return;
  }

  if (!converting) {
    if ((long)(millis() - nextAt) >= 0) {
      startConversion();
      converting = true;
      readyAt = millis() + CONVERT_MS;
    }
    return;
  }

  if ((long)(millis() - readyAt) < 0) return;      // conversion still running

  converting = false;
  nextAt = millis() + READ_PERIOD_MS - CONVERT_MS; // keeps the period near READ_PERIOD_MS

  float c;
  if (!readTempC(c)) {
    Serial.println("Bad probe read (CRC, no reply, or out of range); keeping the last advertisement.");
    return;
  }
  haveGood = true;
  packetId++;
  advertiseTemperature(c);

  Serial.print("pkt ");  Serial.print(packetId);
  Serial.print("  ");    Serial.print(c, 2);
  Serial.print(" C  ");  Serial.print(c * 1.8f + 32.0f, 1);
  Serial.println(" F");
}
