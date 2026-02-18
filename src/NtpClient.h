#pragma once

/**
 * @file NtpClient.h
 * @brief NTP Time Synchronization Library for Arduino
 * 
 * NTPSharp is a fast, accurate time synchronization library using NTP with
 * automatic drift correction. It provides cross-platform support for ESP8266, ESP32,
 * and standard Arduino boards with WiFi or Ethernet connectivity.
 * 
 * @author Costin Bobes
 * @version 1.0
 * @date 2026
 * 
 * ## Features
 * - NTP time synchronization with configurable server addresses
 * - Support for multiple platforms (ESP8266, ESP32, Arduino with Ethernet/WiFi)
 * - Gradual time adjustments without sudden time jumps (except for large offsets)
 * - Internal clock drift calculation and correction using low-pass filtering
 * - Fast catch-up correction mode for large time offsets
 * - Fallback to internal clock with drift correction when NTP server is unreachable
 * - Low memory footprint suitable for resource-constrained IoT environments
 * 
 * ## Basic Usage
 * @code
 * #include <NtpClient.h>
 * #include <WiFi.h>
 * 
 * WiFiUDP ntpUDP;
 * NtpClient timeClient(ntpUDP);
 * 
 * void setup() {
 *   Serial.begin(115200);
 *   WiFi.begin("SSID", "PASSWORD");
 *   timeClient.begin();
 * }
 * 
 * void loop() {
 *   timeClient.update();
 *   
 *   uint64_t now = timeClient.getTime();
 *   Serial.println(timeClient.getFormattedTime());
 *   
 *   delay(5000);
 * }
 * @endcode
 * 
 * @see https://github.com/costinbobes/NTPSharp
 */

#include <Arduino.h>
#include <Udp.h>

// ===== NTP Client Logging Configuration =====
/**
 * @defgroup Logging Logging Configuration
 * @{
 * Define NTP_LOG_LEVEL before including this header to control logging verbosity.
 * 
 * Available levels:
 * - @c NTP_LOG_NONE (0): No logging (default)
 * - @c NTP_LOG_ERROR (1): Only error messages
 * - @c NTP_LOG_INFO (2): Info and error messages
 * - @c NTP_LOG_DEBUG (3): All messages including debug
 * 
 * ### Example
 * @code
 * #define NTP_LOG_LEVEL NTP_LOG_DEBUG
 * #include <NtpClient.h>
 * @endcode
 * @}
 */
#ifndef NTP_LOG_LEVEL
#define NTP_LOG_LEVEL 0  // Default: no logging
#endif

#define NTP_LOG_NONE  0
#define NTP_LOG_ERROR 1
#define NTP_LOG_INFO  2
#define NTP_LOG_DEBUG 3

// Logging macros - only compile code if level threshold is met
#if NTP_LOG_LEVEL >= NTP_LOG_DEBUG
#define NTP_LOG_DEBUG_MSG(fmt, ...) Serial.printf("[NTP-D] " fmt, ##__VA_ARGS__)
#else
#define NTP_LOG_DEBUG_MSG(fmt, ...)
#endif

#if NTP_LOG_LEVEL >= NTP_LOG_INFO
#define NTP_LOG_INFO_MSG(fmt, ...) Serial.printf("[NTP-I] " fmt, ##__VA_ARGS__)
#else
#define NTP_LOG_INFO_MSG(fmt, ...)
#endif

#if NTP_LOG_LEVEL >= NTP_LOG_ERROR
#define NTP_LOG_ERROR_MSG(fmt, ...) Serial.printf("[NTP-E] " fmt, ##__VA_ARGS__)
#else
#define NTP_LOG_ERROR_MSG(fmt, ...)
#endif

// Convenience: log unconditionally (when NTP_LOG_LEVEL > NONE)
#if NTP_LOG_LEVEL > NTP_LOG_NONE
#define NTP_LOG(fmt, ...) Serial.printf("[NTP] " fmt, ##__VA_ARGS__)
#else
#define NTP_LOG(fmt, ...)
#endif

typedef struct NtpTime {
	uint64_t time;      ///< Unix timestamp in milliseconds since 1970-01-01 00:00:00 UTC
	bool success;       ///< True if the NTP update was successful, false otherwise
} NtpTime;

enum class NtpState : uint8_t {
	UNINIT,			///< Library not initialized
	INIT,           ///< Waiting for first NTP response after initialization
	SYNCED,         ///< Normal operation with valid time and drift calculation
	CATCHING_UP		///< Fast correction mode due to large time offset
};

class NtpClient {
private:
	/// @name Configuration Constants
	/// @{
	static constexpr uint32_t NTP_MINIMUM_UPDATE_INTERVAL = 60000; ///< Minimum update interval (1 minute)
	static constexpr uint32_t NTP_MAXIMUM_UPDATE_INTERVAL = 86400000; ///< Maximum update interval (1 day)
	static constexpr uint32_t NTP_DEFAULT_UPDATE_INTERVAL = 900000; ///< Default update interval (15 minutes)
	static constexpr uint32_t NTP_ROLLOVER_PIVOT = 3976214400UL; ///< Rollover pivot (Jan 1, 2026; valid until Feb 7, 2036)
	static constexpr uint64_t YEAR1970STAMP_MS = 2208988800000ULL; ///< Unix epoch offset in milliseconds
	static constexpr uint8_t NTP_PACKET_SIZE = 48; ///< Standard NTP packet size
	static constexpr uint16_t NTP_DEFAULT_LOCAL_PORT = 1337; ///< Default local UDP port
	static constexpr uint32_t NTP_FAIL_MIN_WAIT_INTERVAL = 10000; ///< Minimum retry interval (10 seconds)
	static constexpr uint32_t NTP_FAIL_MAX_WAIT_INTERVAL = 180000; ///< Maximum retry interval (3 minutes)
	static constexpr uint32_t CATCHUP_THRESHOLD = 1000; ///< Threshold for entering catch-up mode (1 second)
	static constexpr uint32_t MAX_ACCEPTABLE_DELAY = 2000; ///< Maximum acceptable delay for NTP response (2 seconds)
	/// @}

	/// @name Network Parameters
	/// @{
	UDP* _udp; ///< UDP socket for NTP communication
	bool _isUdpInit = false; ///< UDP initialization flag
	const char* _poolServerName = "pool.ntp.org"; ///< NTP server hostname
	IPAddress _poolServerIP; ///< NTP server IP address
	int _port = NTP_DEFAULT_LOCAL_PORT; ///< Local UDP port
	bool _updateTriggered = false; ///< Flag to trigger an NTP update
	byte _packetBuffer[NTP_PACKET_SIZE]; ///< Buffer for NTP packet
	/// @}

	/// @name Time Management Parameters
	/// @{
	uint64_t _ntpTime = 0; ///< Base time in ms since 1970 (last anchored time)
	NtpState _state = NtpState::UNINIT; ///< Current state machine state
	bool _isStale = true; ///< Marks time data as stale and unusable
	bool _neverUpdated = true; ///< True until first successful NTP response
	uint32_t _updateInterval = 0; ///< Update interval in milliseconds
	int32_t _runningOffset = 0; ///< Current offset vs NTP server (ms)
	uint32_t _lastLocalMillisUpdate = 0; ///< Last local time of NTP update
	uint32_t _lastLocalMillisNtpReceived = 0; ///< Local millis() when last NTP packet was received (for age calculation)
	uint32_t _lastLocalMillisAttempt = 0; ///< Last time an update was attempted
	uint64_t _lastNtpTimeReceived = 0; ///< Last NTP time received from server (ms)
	float _computedClockDrift = 0.0f; ///< Computed/smoothed drift including offset compensation (ppm)
	float _baseDrift = 0.0f; ///< Base drift from measurements only (ppm)
	float _userDrift = 0.0f; ///< User-specified drift (preserved across catch-up) (ppm)
	float _catchUpDrift = 0.0f; ///< Drift during catch-up phase (ppm)
	uint32_t _lastReferenceMillis = 0; ///< Last local millis() snapshot for drift-adjusted time
	uint64_t _accumulatedReferenceMillis = 0; ///< Accumulated drift-adjusted time since _ntpTime
	uint32_t _lastNtpDelay = 0; ///< Last measured round-trip time to NTP server (ms)
	/// @}

	/// @name State Tracking Parameters
	/// @{
	unsigned int _failCount = 0; ///< Count of failed update attempts
	uint32_t _lastBackoffInterval = NTP_FAIL_MIN_WAIT_INTERVAL; ///< Current backoff retry interval
	int64_t _lastClockOffset = 0; ///< Last calculated offset at the moment of update (ms)
	uint16_t _instantOffsetCorrection = 3600; ///< Threshold for instant correction (seconds, default 1 hour)
	uint16_t _catchUpCorrectionInterval = 300; ///< Catch-up correction interval (seconds, default 5 minutes)
	bool _skipNextDriftCalc = false; ///< Skip drift calculation on next NTP response (set after setTime)
	/// @}
	
	void sendNTPPacket();
	uint32_t getMillisSpan(uint32_t fromTime, uint32_t toTime) const; ///< Calculate time span accounting for uint32 rollover

	/// @name State Machine Helpers
	/// @{
	void setMachineState(NtpState newState); ///< Set the internal state machine state
	/// @}

	/// @name Internal Update Methods
	/// @{
	bool checkUpdateInternal(uint32_t millisNow); ///< Internal method to check and process NTP updates
	uint32_t updateBackoffInterval(); ///< Calculate next backoff retry interval
	/// @}

	/// @name Drift Management
	/// @{
	void setCatchUpDrift(float ppm); ///< Set drift during catch-up correction phase
	void anchorTime(); ///< Snapshot current time and reset accumulated/reference millis
	/// @}

	typedef void (*callback_function)(NtpTime); ///< Callback function type for update notifications
	callback_function _callbackUpdate = NULL; ///< Callback invoked on successful NTP updates

public:
	/// @name Constructors
	/// @{
	/**
	 * @brief Create an NTP client with default server.
	 * @param udp Reference to a UDP object for network communication.
	 */
	NtpClient(UDP& udp);

	/**
	 * @brief Create an NTP client with a named NTP server.
	 * @param udp Reference to a UDP object for network communication.
	 * @param poolServerName Hostname of the NTP server (e.g., "pool.ntp.org").
	 */
	NtpClient(UDP& udp, const char* poolServerName);

	/**
	 * @brief Create an NTP client with an IP address NTP server.
	 * @param udp Reference to a UDP object for network communication.
	 * @param poolServerIP IP address of the NTP server.
	 */
	NtpClient(UDP& udp, IPAddress poolServerIP);
	/// @}

	/// @name Initialization and Configuration
	/// @{
	/**
	 * @brief Set a callback function to be invoked on NTP updates.
	 * @param callback Function pointer of type callback_function, called with NtpTime data.
	 */
	void setUpdateCallback(callback_function callback);

	/**
	 * @brief Set the NTP server hostname.
	 * @param poolServerName Hostname of the NTP server (e.g., "pool.ntp.org").
	 */
	void setPoolServerName(const char* poolServerName);

	/**
	 * @brief Initialize the NTP client with default port.
	 * Starts the UDP socket and enters INIT state. Must be called before update().
	 */
	void begin();

	/**
	 * @brief Initialize the NTP client with a custom port.
	 * @param port Local UDP port to listen on (default: 1337).
	 */
	void begin(int port);

	/**
	 * @brief Shut down the NTP client and close the UDP socket.
	 */
	void end();
	/// @}

	/// @name Time Retrieval
	/// @{
	/**
	 * @brief Get the current time.
	 * Returns the current time in milliseconds since 1970-01-01 00:00:00 UTC,
	 * adjusted for drift. If no valid time is available, returns 0.
	 * 
	 * @return Time in milliseconds since Unix epoch.
	 */
	uint64_t getTime() const;

	/**
	 * @brief Get the current time as a 32-bit value (legacy).
	 * 
	 * @warning This function will roll over! Affected by the 2036 and 2038 issues.
	 * For new code, use getTime() instead.
	 * 
	 * @return Time in seconds since Unix epoch (truncated to 32 bits).
	 */
	uint32_t getLegacyTime();


	/**
	 * @brief Get the last measured round-trip time to the NTP server.
	 * 
	 * This is the time it took for the last NTP request to receive a response,
	 * measured in milliseconds. Useful for monitoring network latency and
	 * diagnosing connectivity issues.
	 * 
	 * @return Round-trip time in milliseconds (0 if no valid measurement).
	 */
	uint32_t getLastNtpDelay() const; ///< Get the last measured round-trip time to the NTP server (ms)

	/**
	 * @brief Get the current time formatted as ISO 8601 string.
	 * Format: YYYY-MM-DDTHH:MM:SSZ
	 * 
	 * @return Pointer to a static buffer containing the formatted time string.
	 *         Buffer is overwritten on each call.
	 */
	char* getFormattedTime();
	/// @}

	/// @name Update and Synchronization
	/// @{
	/**
	 * @brief Main update function - must be called regularly in loop().
	 * Checks if an NTP update is needed and processes incoming NTP responses.
	 * Non-blocking operation.
	 */
	void update();

	/**
	 * @brief Trigger an immediate NTP update.
	 * Fire-and-forget; the actual update occurs during the next update() call.
	 * 
	 * @return Always returns true.
	 */
	bool forceUpdate();

	/**
	 * @brief Set the interval between NTP synchronization updates.
	 * @param updateInterval Interval in seconds (default: 900 = 15 minutes).
	 * 
	 * Valid range: 60 seconds to 86400 seconds (1 day).
	 * Values outside this range are clamped.
	 */
	void setUpdateInterval(uint32_t updateInterval);
	/// @}

	/// @name Time Validation
	/// @{
	/**
	 * @brief Check if the current time is valid and synchronized.
	 * @return True if time has been set (via NTP or setTime()), false otherwise.
	 */
	bool isSet() const;
	/// @}

	/// @name Drift and Offset Management
	/// @{
	/**
	 * @brief Get the current clock drift in parts per million (ppm).
	 * 
	 * Positive drift = local clock runs fast.
	 * Negative drift = local clock runs slow.
	 * 
	 * During CATCHING_UP state, returns the catch-up drift; otherwise returns
	 * the computed drift (which includes offset compensation).
	 * 
	 * @return Drift in ppm (parts per million).
	 */
	float getDrift() const;

	/**
	 * @brief Manually set the local clock drift.
	 * 
	 * Anchors the current time before applying the new drift.
	 * Useful for pre-compensating for known oscillator drift.
	 * 
	 * @param ppm Drift in ppm; clamped to ±200,000 ppm (±20%).
	 */
	void setLocalClockDrift(float ppm);

	/**
	 * @brief Set the threshold for instant time correction.
	 * 
	 * If the offset between internal time and NTP time exceeds this threshold,
	 * the time is corrected instantly (no gradual correction).
	 * 
	 * @param offset Threshold in seconds (default: 3600 = 1 hour).
	 */
	void setInstantOffsetCorrection(int32_t offset);

	/**
	 * @brief Set the interval for catch-up correction.
	 * 
	 * When entering CATCHING_UP mode, the internal clock will gradually drift
	 * toward the correct NTP time over this interval.
	 * 
	 * @param interval Interval in seconds; clamped to 60-3600 seconds
	 *                 (default: 300 = 5 minutes).
	 */
	void setCatchUpCorrectionInterval(uint32_t interval);

	/**
	 * @brief Manually set the current time.
	 * 
	 * Sets the internal time reference and triggers an immediate NTP update
	 * to verify/synchronize with the NTP server.
	 * 
	 * @param time Unix timestamp in milliseconds since 1970-01-01 00:00:00 UTC.
	 */
	void setTime(uint64_t time);
	/// @}

	/// @name Diagnostics and Monitoring
	/// @{
	/**
	 * @brief Get the age of the last NTP sample.
	 * 
	 * Returns how long ago the last successful NTP response was received,
	 * measured using local millis() to avoid being affected by time offsets.
	 * Useful for determining if a fresh sync is needed.
	 * 
	 * @return Age in seconds (0 if no valid time or time is stale).
	 */
	uint32_t getNTPAge();

	/**
	 * @brief Get the last calculated clock offset.
	 * 
	 * Returns the offset between the internal clock and the NTP time
	 * at the moment of the last successful NTP update.
	 * 
	 * Positive = internal clock ahead.
	 * Negative = internal clock behind.
	 * 
	 * @return Offset in milliseconds.
	 */
	int64_t getLastClockOffset() const;

	/**
	 * @brief Get the last NTP timestamp received from the server.
	 * 
	 * This is the actual NTP time that was received during the last
	 * successful synchronization. Can be used to monitor sync freshness.
	 * 
	 * @return Unix timestamp in milliseconds since 1970-01-01 00:00:00 UTC.
	 */
	uint64_t getLastNtpUpdateTime() const;
	/// @}
};
