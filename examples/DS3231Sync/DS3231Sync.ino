/*
 * NtpClient + DS3231 RTC Synchronization Example
 *
 * This example demonstrates how to use a DS3231 real-time clock as a time
 * source for initial boot (before WiFi/NTP is available), and then keep
 * the DS3231 updated with accurate NTP-corrected time on each sync.
 *
 * Flow:
 *   1. On startup, read the DS3231 and seed NtpClient via setTime()
 *   2. NtpClient syncs with NTP and corrects drift automatically
 *   3. On every successful NTP update, write the corrected time back to DS3231
 *
 * Hardware:
 *   - ESP8266 or ESP32
 *   - DS3231 RTC module connected via I2C (SDA/SCL)
 *
 * Dependencies:
 *   - RTClib by Adafruit (install via Arduino Library Manager)
 *   - NTPSharp (this library)
 */

#include <Wire.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NtpClient.h>

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// RTC and NTP instances
RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NtpClient ntpClient(ntpUDP, "pool.ntp.org");

// Timing
uint32_t lastPrintTime = 0;

// Convert a DateTime (from RTClib) to Unix timestamp in milliseconds
uint64_t dateTimeToMillis(const DateTime& dt) {
  return (uint64_t)dt.unixtime() * 1000ULL;
}

// Write the current NTP-corrected time back to the DS3231
void writeTimeToRTC(uint64_t timeMs) {
  uint32_t unixSec = (uint32_t)(timeMs / 1000ULL);
  DateTime dt(unixSec);
  rtc.adjust(dt);
  Serial.print("RTC updated: ");
  Serial.println(ntpClient.getFormattedTime());
}

void onNtpUpdate(NtpTime data) {
  if (data.success) {
    Serial.println("✓ NTP sync successful!");

    // Write NTP-corrected time back to DS3231
    writeTimeToRTC(data.time);
  } else {
    Serial.println("✗ NTP sync failed!");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\nNtpClient + DS3231 RTC Sync Example");
  Serial.println("====================================\n");

  // Initialize I2C and DS3231
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("ERROR: DS3231 not found! Check wiring.");
  } else {
    // Read current time from DS3231 and seed NtpClient
    DateTime now = rtc.now();
    uint64_t rtcTimeMs = dateTimeToMillis(now);

    Serial.print("DS3231 time: ");
    Serial.print(now.year());
    Serial.print("-");
    if (now.month() < 10) Serial.print("0");
    Serial.print(now.month());
    Serial.print("-");
    if (now.day() < 10) Serial.print("0");
    Serial.print(now.day());
    Serial.print("T");
    if (now.hour() < 10) Serial.print("0");
    Serial.print(now.hour());
    Serial.print(":");
    if (now.minute() < 10) Serial.print("0");
    Serial.print(now.minute());
    Serial.print(":");
    if (now.second() < 10) Serial.print("0");
    Serial.println(now.second());

    // Seed the NTP client with RTC time so getTime()/isSet() work immediately
    ntpClient.setTime(rtcTimeMs);
    Serial.println("NTP client seeded with DS3231 time.");
  }

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi not available, running on DS3231 time.");
  }

  // Configure NTP client
  ntpClient.setUpdateCallback(onNtpUpdate);
  ntpClient.setUpdateInterval(900); // 15 minutes - you should *not* set this too low in production, this is just for testing
  ntpClient.begin();

  Serial.println("NTP client initialized.\n");
}

void loop() {
  ntpClient.update();

  // Print time every 5 seconds
  if (millis() - lastPrintTime >= 5000) {
    lastPrintTime = millis();

    if (ntpClient.isSet()) {
      Serial.print(ntpClient.getFormattedTime());
      Serial.print(" | Drift: ");
      Serial.print(ntpClient.getDrift());
      Serial.print(" ppm | NTP age: ");
      Serial.print(ntpClient.getNTPAge());
      Serial.println("s");
    } else {
      Serial.println("No valid time available.");
    }
  }
}
