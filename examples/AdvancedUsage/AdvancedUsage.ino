#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NtpClient.h>

// Replace with your WiFi credentials or use a separate config file for production
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

WiFiUDP udp;
NtpClient ntpClient(udp);

static uint32_t lastWifiCheck = 0;
static const uint32_t WIFI_CHECK_INTERVAL = 30000; // 30 seconds

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi: Connecting...");
  WiFi.begin(ssid, password);

  uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi: Connected");
    Serial.print("WiFi: IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi: Connection failed");
  }
}

void checkWifiReconnect() {
  uint32_t now = millis();
  if (now - lastWifiCheck < WIFI_CHECK_INTERVAL) return;
  lastWifiCheck = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: Disconnected, attempting reconnection...");
    WiFi.disconnect();
    connectWifi();
    if (WiFi.status() == WL_CONNECTED) {
      ntpClient.begin(); // re-initialize UDP after reconnection
    }
  }
}

void ntpCallback(NtpTime data) {
  if (data.success) {
    Serial.printf("NTP callback: time updated, UTC = %llu\n", data.time);
  } else {
    Serial.println("NTP callback: update failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectWifi();

  ntpClient.setUpdateInterval(900); // 15 minutes in seconds
  ntpClient.setUpdateCallback(ntpCallback);
  ntpClient.begin();
}

void loop() {
  checkWifiReconnect();
  ntpClient.update();

  // Print time periodically for debug
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 10000) {
    lastPrint = millis();
    Serial.printf("Current time: %s  Drift: %.2f ppm\n", ntpClient.getFormattedTime(), ntpClient.getDrift());
  }
}