/*
 * NtpClient Basic Example
 * 
 * This example demonstrates the basic usage of the NtpClient library.
 * It connects to WiFi, synchronizes time via NTP, and prints the current time every second.
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NtpClient.h>

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// NTP Server
const char* ntpServer = "pool.ntp.org";

// UDP and NTP client instances
WiFiUDP ntpUDP;
NtpClient ntpClient(ntpUDP, ntpServer);

// Timing variables
uint32_t lastPrintTime = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\nNtpClient Example - Basic Usage");
  Serial.println("================================\n");
  
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
    Serial.println("\nFailed to connect to WiFi!");
  }
  
  // Set NTP update callback
  ntpClient.setUpdateCallback(onNtpUpdate);
  
  // Optional: Set update interval
  ntpClient.setUpdateInterval(900); // 15 minutes - you should *not* set this too low in production, this is just for testing
  
  // Initialize NTP client
  ntpClient.begin();
  
  Serial.println("NTP client initialized. Waiting for first sync...\n");
}

void loop() {
  // Update NTP time periodically
  ntpClient.update();
  
  // Print time every second
  if (millis() - lastPrintTime >= 1000) {
    lastPrintTime = millis();
    
    if (ntpClient.isSet()) {
      Serial.print("Time: ");
      Serial.print(ntpClient.getFormattedTime());
      Serial.print(" | Drift: ");
      Serial.print(ntpClient.getDrift());
      Serial.println(" ppm");
    } else {
      Serial.println("Waiting for NTP synchronization...");
    }
  }
}

void onNtpUpdate(NtpTime data) {
  if (data.success) {
    Serial.println("✓ NTP sync successful!");
  } else {
    Serial.println("✗ NTP sync failed!");
  }
}
