# NTPSharp

A fast, accurate time synchronization library for Arduino using NTP (Network Time Protocol) with automatic local clock drift correction.

## IMPORTANT NOTE

Do not set the update interval too low. You will get better long-term stability and accuracy with an update interval of at least 30 min (1800 seconds) and ideally 2+ hours (7200+ seconds).
A good starting point is 2 hours (7200 seconds) and then adjust based on observed drift and network conditions, although you shouldn't have to - the library will automatically calculate and compensate for drift over time.
An interval which is too low will cause the network latency to be significant with respect to the elapsed time and will cause the system to continuously overcorrect.
Example: 30 ms network jitter over 300 s is 100 ppm – which may be 10× higher than the actual drift of the clock.
Furthermore, an interval which is too low may cause NTP servers to block such clients.

## Note on Arduino Uno and similar 8-bit AVR boards

While this library can be used on Arduino Uno and similar 8-bit AVR boards, it is not recommended due to the limited resources of these platforms. The library's features, such as drift correction and comprehensive logging, may consume more memory and processing power than what these boards can efficiently handle.
These boards have a millis() function which drifts significantly due to architectural limitations.
The internal clock ticks at 16MHz and the derived clock is 1.024ms instead of 1ms; the system applies "leap milliseconds" to keep it somewhat in sync.
Many times cheap boards use piezo resonators with a drift of up to 2000 ppm (0.2%) or more, which means they can drift by several minutes per day.
The library will still work, but you may experience significant time drift and less accurate synchronization. For better performance and accuracy, consider using more capable boards like the ESP8266 or ESP32, which have more stable clocks and can handle the library's features more effectively.


## Features

- **NTP Time Synchronization**: Synchronizes with NTP servers (default: pool.ntp.org)
- **Cross-Platform Support**: Works with ESP8266, ESP32, and Arduino boards with WiFi/Ethernet
- **Drift Correction**: Automatically calculates and compensates for local oscillator drift
- **No Time Jumps**: Gradual time adjustments (except for very large offsets)
- **Fast Catch-Up**: Rapid convergence when there are large time offset corrections needed
- **Fallback Clock**: Maintains accurate time using internal clock when NTP is unavailable
- **Low Memory Footprint**: Efficient implementation suitable for IoT devices
- **Configurable**: Customize servers, update intervals, correction thresholds, and callbacks
- **Comprehensive Logging**: Optional debug logging for troubleshooting

## Installation

### Using Arduino IDE

1. Sketch → Include Library → Add .ZIP Library
2. Select the NTPSharp folder
3. Include in your sketch: `#include <NtpClient.h>`

### Manual Installation

Copy the library folder to your Arduino libraries directory:
- **Windows**: `Documents\Arduino\libraries\`
- **Mac**: `~/Documents/Arduino/libraries/`
- **Linux**: `~/Arduino/libraries/`

## Quick Start

### ESP8266 / ESP32 Example

```cpp
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NtpClient.h>

WiFiUDP ntpUDP;
NtpClient timeClient(ntpUDP);

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  WiFi.begin("SSID", "PASSWORD");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // Start NTP client
  timeClient.begin();
  timeClient.setUpdateInterval(3600); // 1 hour
}

void loop() {
  timeClient.update();
  
  if (timeClient.isSet()) {
    Serial.println(timeClient.getFormattedTime());
  }
  
  delay(1000);
}
```

### Arduino with Ethernet Shield Example

```cpp
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <NtpClient.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
EthernetUDP Udp;
NtpClient timeClient(Udp);

void setup() {
  Serial.begin(9600);
  
  // Initialize Ethernet
  Ethernet.begin(mac);
  
  // Start NTP client
  timeClient.begin();
}

void loop() {
  timeClient.update();
  
  Serial.println(timeClient.getFormattedTime());
  delay(1000);
}
```

### More Examples

- **[BasicUsage](examples/BasicUsage/BasicUsage.ino)** – Minimal ESP8266 setup with time printing
- **[AdvancedUsage](examples/AdvancedUsage/AdvancedUsage.ino)** – ESP32 with WiFi reconnection handling
- **[DS3231Sync](examples/DS3231Sync/DS3231Sync.ino)** – Seed time from a DS3231 RTC on boot, write NTP-corrected time back on each sync

## API Reference

### Initialization

#### `NtpClient(UDP& udp)`
Create an NTP client with default settings (pool.ntp.org).

#### `NtpClient(UDP& udp, const char* poolServerName)`
Create an NTP client with a specific server hostname.

#### `NtpClient(UDP& udp, IPAddress poolServerIP)`
Create an NTP client with a specific server IP address.

#### `void begin()`
Initialize the NTP client with the default port (1337).

#### `void begin(int port)`
Initialize the NTP client with a custom UDP port.

#### `void end()`
Shut down the NTP client and close the UDP socket.

### Time Retrieval

#### `uint64_t getTime()`
Get the current time in milliseconds since Unix epoch (1970-01-01 00:00:00 UTC).

**Returns**: Time in ms, adjusted for drift.

#### `uint32_t getLegacyTime()`
Get the current time in seconds since Unix epoch (for backward compatibility).

⚠️ **Warning**: This function truncates to 32-bit and will overflow in 2036/2038. Use `getTime()` for new code.

#### `char* getFormattedTime()`
Get the current time as an ISO 8601 formatted string (YYYY-MM-DDTHH:MM:SSZ).

**Returns**: Pointer to static buffer (overwritten on next call).

#### `bool isSet()`
Check if the time has been successfully synchronized.

**Returns**: `true` if time is valid, `false` otherwise.

### Updates and Synchronization

#### `void update()`
Main update function. Call regularly (in `loop()`).

Checks if an NTP update is needed and processes responses. Non-blocking.

#### `bool forceUpdate()`
Trigger an immediate NTP update on the next `update()` call.

**Returns**: Always `true` (fire-and-forget).

#### `void setUpdateInterval(uint32_t updateInterval)`
Set the interval between NTP synchronization updates.

**Parameters**:
- `updateInterval`: Interval in seconds (valid range: 60-86400, default: 900)

#### `void setTime(uint64_t time)`
Manually set the current time and trigger a sync verification.

**Parameters**:
- `time`: Unix timestamp in milliseconds.

### Drift Management

#### `float getDrift()`
Get the current clock drift.

**Returns**: Drift in ppm (parts per million)
- Positive = clock runs fast
- Negative = clock runs slow

#### `void setLocalClockDrift(float ppm)`
Manually set (pre-compensate) the local clock drift.

**Parameters**:
- `ppm`: Drift value; clamped to ±200,000 ppm.

#### `void setInstantOffsetCorrection(int32_t offset)`
Set the threshold for instant time correction.

**Parameters**:
- `offset`: Threshold in seconds (default: 3600)

If the offset exceeds this, the time is corrected instantly.

#### `void setCatchUpCorrectionInterval(uint32_t interval)`
Set the duration for gradual time correction during catch-up.

**Parameters**:
- `interval`: Duration in seconds; clamped to 60-3600 (default: 300)

### Diagnostics

#### `uint32_t getNTPAge()`
Get the age of the last NTP sample.

**Returns**: Age in seconds since last successful NTP update (0 if no valid time).

#### `int64_t getLastClockOffset()`
Get the last calculated offset at the moment of the most recent NTP update.

**Returns**: Offset in milliseconds
- Positive = internal clock ahead
- Negative = internal clock behind

#### `uint64_t getLastNtpUpdateTime()`
Get the actual NTP timestamp received during the last successful update.

**Returns**: Unix timestamp in milliseconds.

#### `uint32_t getLastNtpDelay()`
Get the last measured round-trip time to the NTP server.

Useful for monitoring network latency and diagnosing connectivity issues.

**Returns**: Round-trip time in milliseconds (0 if no valid measurement).

### Callbacks

#### `void setUpdateCallback(callback_function callback)`
Register a callback function to be invoked on NTP updates.

**Parameters**:
- `callback`: Function pointer with signature `void callback(NtpTime data)`

**Callback Data Structure**:
```cpp
struct NtpTime {
  uint64_t time;    // The synchronized time
  bool success;     // true if sync was successful
};
```

### Configuration

#### `void setPoolServerName(const char* poolServerName)`
Change the NTP server hostname.

**Parameters**:
- `poolServerName`: Hostname or domain name of NTP server

## Time Correction Modes

### Normal Synchronization (SYNCED state)

Once synchronized, the library:
1. Calculates the local oscillator drift based on NTP updates
2. Applies offset compensation to keep time accurate
3. Uses a low-pass filter to smooth drift changes
4. Maintains time using the internal clock between updates

### Catch-Up Correction (CATCHING_UP state)

When the offset exceeds `CATCHUP_THRESHOLD` (1 second):
1. Enters CATCHING_UP mode
2. Applies a calculated drift to gradually reach the correct time
3. Completes correction within `_catchUpCorrectionInterval`
4. Returns to SYNCED mode and normal drift operation

### Instant Correction

When the offset exceeds `_instantOffsetCorrection` (default: 1 hour):
1. Time is set instantly to the NTP value (no gradual correction)
2. Library immediately returns to SYNCED mode
3. Normal drift calculation resumes

## Drift Calculation

### How Drift is Calculated

```
drift_ppm = (localElapsed - ntpElapsed) / ntpElapsed * 1,000,000
```

- **Positive drift** = local clock is FAST
- **Negative drift** = local clock is SLOW

### How Drift is Applied

```
realTime = millisReading / (1 + drift_ppm / 1,000,000)
```

### Example

If local oscillator runs 1000 ppm fast:
- Real time passes: 1,000,000 ms
- Local millis() reports: 1,001,000 ms (too fast)
- Calculated drift: +1000 ppm
- To get real time: 1,001,000 / 1.001 = 1,000,000 ms ✓

## Logging

Enable debug logging by defining `NTP_LOG_LEVEL` before including the library:

```cpp
#define NTP_LOG_LEVEL NTP_LOG_DEBUG
#include <NtpClient.h>
```

**Log Levels**:
- `NTP_LOG_NONE` (0): No logging
- `NTP_LOG_ERROR` (1): Errors only
- `NTP_LOG_INFO` (2): Informational messages
- `NTP_LOG_DEBUG` (3): All messages

Output goes to `Serial` (configure baud rate appropriately).

## Performance Considerations

- **Memory**: ~2 KB of RAM
- **Network**: Requires WiFi or Ethernet connectivity
- **CPU**: Minimal overhead; update processing takes <10ms
- **Update Frequency**: Default 15 minutes; adjustable via `setUpdateInterval()`

## Troubleshooting

### Time not synchronized
- Check WiFi/Ethernet connection
- Verify NTP server is reachable
- Monitor logs with `NTP_LOG_DEBUG`
- Confirm UDP port (default 1337) is not blocked

### Inaccurate time / High drift
- Large drift suggests hardware issue or temperature effects
- Pre-compensate with `setLocalClockDrift()` if drift is known
- Increase update interval for better averaging

### Time jumps backward
- This library never allows backward time jumps
- Only gradual slowing down occurs until offset is corrected
- Instant corrections only apply when offset > `_instantOffsetCorrection`

## Theory of Operation

1. **Initialization**: Sets machine state to INIT, waiting for first NTP response
2. **First Sync**: Receives NTP time, enters SYNCED state, records baseline
3. **Periodic Updates**: Receives periodic NTP updates, calculates drift
4. **Drift Smoothing**: Uses exponential moving average to smooth drift changes
5. **Offset Compensation**: Applies low-pass filter to offset-based correction
6. **Time Maintenance**: Between updates, uses internal clock + calculated drift
7. **Catch-Up**: When offset > 1s, enters CATCHING_UP for gradual correction
8. **Fallback**: If NTP unreachable, maintains time via local clock with drift

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## References

- [NTP Protocol (RFC 5905)](https://tools.ietf.org/html/rfc5905)
- [pool.ntp.org](https://www.pool.ntp.org/)
- [Arduino Time Libraries](https://www.arduino.cc/reference/en/)

---

**Author**: Costin Bobes  
**Repository**: https://github.com/costinbobes/NTPSharp

