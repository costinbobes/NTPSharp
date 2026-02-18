/*
 * @file NtpClient.cpp
 * @brief Implementation of NTP time synchronization client.
 * 
 * This file contains the core functionality for NTP time synchronization,
 * including drift calculation, catch-up correction, and time management.
 */

#include "NtpClient.h"

NtpClient::NtpClient(UDP& udp) {
    _udp = &udp;
}

NtpClient::NtpClient(UDP& udp, const char* poolServerName) {
    _udp = &udp;
    _poolServerName = poolServerName;
}

NtpClient::NtpClient(UDP& udp, IPAddress poolServerIP) {
    _udp = &udp;
    _poolServerIP = poolServerIP;
    _poolServerName = NULL;
}

void NtpClient::begin() {
    begin(NTP_DEFAULT_LOCAL_PORT);
}

void NtpClient::begin(int port) {
    _port = port;

    _udp->begin(_port);

    _isUdpInit = true;
	if(_updateInterval==0) _updateInterval = NTP_DEFAULT_UPDATE_INTERVAL;
    _failCount = 0;
	_lastBackoffInterval = NTP_FAIL_MIN_WAIT_INTERVAL;
    setMachineState(NtpState::INIT);
}

bool NtpClient::forceUpdate() {
    if (!_updateTriggered) {
        _updateTriggered = true;
		_lastLocalMillisAttempt = millis();
    }
	return true; // Fire and forget
}

void NtpClient::sendNTPPacket() {
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    _packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    _packetBuffer[1] = 0;     // Stratum, or type of clock
    _packetBuffer[2] = 6;     // Polling Interval
    _packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    _packetBuffer[12] = 49;
    _packetBuffer[13] = 0x4E;
    _packetBuffer[14] = 49;
    _packetBuffer[15] = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    if (_poolServerName) {
        _udp->beginPacket(_poolServerName, 123);
    }
    else {
        _udp->beginPacket(_poolServerIP, 123);
    }
    _udp->write(_packetBuffer, NTP_PACKET_SIZE);
    _udp->endPacket();
}

void NtpClient::setLocalClockDrift(float ppm) {
	// Anchor current time before changing drift
	anchorTime();
	_computedClockDrift = ppm;
	_baseDrift = ppm; // When manually set, base drift equals computed drift

	//limit drift to +/- 200000 PPM (20%)
	if (_computedClockDrift > 200000.0f) _computedClockDrift = 200000.0f;
	if (_computedClockDrift < -200000.0f) _computedClockDrift = -200000.0f;
}

float NtpClient::getDrift() const {
    if (_state == NtpState::CATCHING_UP) {
        return _catchUpDrift;
    }
    return _computedClockDrift;
}

bool NtpClient::isSet() const {
    // Returns true if we have valid time (from NTP sync or manual setTime)
    return !_isStale;
}

void NtpClient::setUpdateCallback(callback_function callback) {
    _callbackUpdate = callback;
}

uint32_t NtpClient::getLegacyTime() {
    uint64_t time = getTime();
    // WARNING: THIS WILL ROLL OVER!!! 2036 and 2038 ISSUES!!!
    return (uint32_t)(time / 1000ULL);
}

char* NtpClient::getFormattedTime() {
    static char timeBuffer[21]; // enough for ISO8601

    uint64_t currentTime = getTime();
    uint32_t currentSec = currentTime / 1000;

    uint8_t second = (uint8_t)(currentSec % 60U);
    uint8_t minute = (uint8_t)((currentSec / 60U) % 60U);
    uint8_t hour = (uint8_t)((currentSec / 3600U) % 24U);
    int32_t days = (int32_t)(currentSec / 86400U); // days since 1970-01-01

    // convert days since 1970-01-01 to Gregorian date
    int32_t z = days + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;                                  // [0, 146096]
    int16_t yoe = (int16_t)((doe - doe / 1460 + doe / 36524 - doe / 146096) / 365);    // [0, 399]
    int16_t year = (int16_t)(yoe + era * 400);
    int16_t doy = (int16_t)(doe - (365 * yoe + yoe / 4 - yoe / 100));                  // [0, 365]
    int16_t mp = (int16_t)((5 * doy + 2) / 153);                                   // [0, 11]
    int16_t day = (int16_t)(doy - (153 * mp + 2) / 5 + 1);                             // [1, 31]
    int16_t month = (int16_t)(mp + (mp < 10 ? 3 : -9));                              // [1, 12]
    year += (month <= 2);

    // format: YYYY-MM-DDTHH:MM:SSZ - manual formatting to avoid snprintf
    char* ptr = timeBuffer;
    *ptr++ = '0' + (year / 1000 % 10);
    *ptr++ = '0' + (year / 100 % 10);
    *ptr++ = '0' + (year / 10 % 10);
    *ptr++ = '0' + (year % 10);
    *ptr++ = '-';
    *ptr++ = '0' + (month / 10);
    *ptr++ = '0' + (month % 10);
    *ptr++ = '-';
    *ptr++ = '0' + (day / 10);
    *ptr++ = '0' + (day % 10);
    *ptr++ = 'T';
    *ptr++ = '0' + (hour / 10);
    *ptr++ = '0' + (hour % 10);
    *ptr++ = ':';
    *ptr++ = '0' + (minute / 10);
    *ptr++ = '0' + (minute % 10);
    *ptr++ = ':';
    *ptr++ = '0' + (second / 10);
    *ptr++ = '0' + (second % 10);
    *ptr++ = 'Z';
    *ptr = '\0';

    return timeBuffer;
}

void NtpClient::end() {
    _udp->stop();

    _isUdpInit = false;
}

void NtpClient::setUpdateInterval(uint32_t updateInterval) {
    const uint32_t minIntervalSeconds = NTP_MINIMUM_UPDATE_INTERVAL / 1000UL;
    const uint32_t maxIntervalSeconds = NTP_MAXIMUM_UPDATE_INTERVAL / 1000UL;
    if (updateInterval < minIntervalSeconds) {
        updateInterval = minIntervalSeconds;
    }
    if (updateInterval > maxIntervalSeconds) {
        updateInterval = maxIntervalSeconds;
    }
    _updateInterval = updateInterval * 1000UL;
}

void NtpClient::setPoolServerName(const char* poolServerName) {
    _poolServerName = poolServerName;
}

void NtpClient::setInstantOffsetCorrection(int32_t offset) {
    _instantOffsetCorrection = offset;
}

void NtpClient::setCatchUpCorrectionInterval(uint32_t interval) {
    if (interval < 60) interval = 60;
    if (interval > 3600) interval = 3600;
    _catchUpCorrectionInterval = interval;
}

uint32_t NtpClient::getMillisSpan(uint32_t fromTime, uint32_t toTime) const {
    if (toTime < fromTime) {
        //rolled over
        return (UINT32_MAX - fromTime) + toTime + 1U;
    }
    return toTime - fromTime;
}

uint32_t NtpClient::getNTPAge() {
	// Return the age (in seconds) of the last NTP sample.
	// If the client has no valid time, report age 0.
	if (_isStale) {
		return 0;
	}
	// Use local millis() elapsed time to avoid being affected by setTime() offsets
	uint32_t ageMs = getMillisSpan(_lastLocalMillisNtpReceived, millis());
	return ageMs / 1000U;
}

void NtpClient::setMachineState(NtpState newState) {
    if (_state == newState) return;
#if NTP_LOG_LEVEL >= NTP_LOG_INFO
    const char* stateNames[] = { "UNINIT", "INIT", "SYNCED", "CATCHING_UP" };
    NTP_LOG_INFO_MSG("State %s -> %s\n", stateNames[(uint8_t)_state], stateNames[(uint8_t)newState]);
#endif
    _state = newState;
}

void NtpClient::setTime(uint64_t time) {
	_ntpTime = time;
	_accumulatedReferenceMillis = 0;
	_lastReferenceMillis = millis();
	_lastNtpTimeReceived = time;
	_lastLocalMillisUpdate = millis();
	_lastLocalMillisNtpReceived = millis();
	_isStale = false;

	// Skip drift calculation on next NTP response since the relationship
	// between local elapsed time and NTP elapsed time is now invalid.
	// Offset evaluation (catch-up, instant correction) still applies.
	_skipNextDriftCalc = true;

	if (_state != NtpState::SYNCED && _state != NtpState::CATCHING_UP) {
		setMachineState(NtpState::SYNCED);
	}

	// Trigger NTP update ASAP to verify the manually set time
	forceUpdate();
	_failCount = 0;
}

uint64_t NtpClient::getTime() const {
    uint32_t elapsedSinceReference = getMillisSpan(_lastReferenceMillis, millis());
    float effectiveDrift = getDrift();
    int64_t driftAdjustedElapsed = (int64_t)((float)elapsedSinceReference / (1.0f + effectiveDrift / 1000000.0f));
    if (driftAdjustedElapsed < 0) driftAdjustedElapsed = 0;
	return _ntpTime + _accumulatedReferenceMillis + (uint64_t)driftAdjustedElapsed;
}

void NtpClient::anchorTime() {
	uint32_t now = millis();
	uint32_t elapsedSinceReference = getMillisSpan(_lastReferenceMillis, now);
	float effectiveDrift = getDrift();
	int64_t driftAdjustedElapsed = (int64_t)((float)elapsedSinceReference / (1.0f + effectiveDrift / 1000000.0f));
	if (driftAdjustedElapsed < 0) driftAdjustedElapsed = 0;
	_ntpTime = _ntpTime + _accumulatedReferenceMillis + (uint64_t)driftAdjustedElapsed;
	_accumulatedReferenceMillis = 0;
	_lastReferenceMillis = now;
}

uint32_t NtpClient::updateBackoffInterval() {
	if (_failCount <= 1) {
        _lastBackoffInterval = NTP_FAIL_MIN_WAIT_INTERVAL;
        return _lastBackoffInterval;
	}
	_lastBackoffInterval = _lastBackoffInterval * 15 / 10;
    if(_lastBackoffInterval > NTP_FAIL_MAX_WAIT_INTERVAL) _lastBackoffInterval = NTP_FAIL_MAX_WAIT_INTERVAL;
    return _lastBackoffInterval;
}

void NtpClient::update() {
    static uint32_t millisNow;
    static uint32_t timeSinceSync;
	millisNow = millis();
    timeSinceSync = getMillisSpan(_lastLocalMillisUpdate, millisNow);

    if (_updateTriggered) {
		checkUpdateInternal(millisNow);
		return;
    }

	if (_state == NtpState::SYNCED) {
		if (_failCount > 0) {
			if (getMillisSpan(_lastLocalMillisAttempt, millisNow) >= _lastBackoffInterval) {
				updateBackoffInterval();
				NTP_LOG_INFO_MSG("backoff interval elapsed, retrying; next backoff set to %lu\n", _lastBackoffInterval);
				_lastLocalMillisAttempt = millisNow;
				_updateTriggered = true;
			}
			return;
		}
		if (timeSinceSync >= _updateInterval) {
			_updateTriggered = true;
			_lastLocalMillisAttempt = millisNow;
		}
		return;
	}

    if(_state == NtpState::UNINIT) {
        return;
	}

	if (_state == NtpState::INIT) {
		if (_failCount > 0) {
			if (getMillisSpan(_lastLocalMillisAttempt, millisNow) >= _lastBackoffInterval) {
				updateBackoffInterval();
				NTP_LOG_INFO_MSG("INIT backoff interval elapsed, retrying; next backoff set to %lu\n", _lastBackoffInterval);
				_lastLocalMillisAttempt = millisNow;
				_updateTriggered = true;
			}
			return;
		}
		_updateTriggered = true;
        return;
    }

	if (_state == NtpState::CATCHING_UP) {
		uint32_t catchUpIntervalMs = (uint32_t)_catchUpCorrectionInterval * 1000U;

		if (timeSinceSync >= catchUpIntervalMs) {
			float driftBefore = getDrift();
			anchorTime();
			_computedClockDrift = _userDrift;
			setMachineState(NtpState::SYNCED);
			NTP_LOG_INFO_MSG("Catch-up interval elapsed, drift changed from %.2f to %.2f ppm\n", driftBefore, _computedClockDrift);
			_lastLocalMillisAttempt = millisNow;
			// Do NOT reset _lastLocalMillisUpdate here: it must stay aligned with
			// _lastNtpTimeReceived so the next drift calculation compares equal time spans
			_updateTriggered = true;
		}
		return;
    }
}

bool NtpClient::checkUpdateInternal(uint32_t millisNow) {
    static uint32_t packetSentStamp, packetReceivedStamp;
	static bool packetSent = false;
	static int16_t averagingIndex = 0;
    if(!_updateTriggered) {
        return true;
	}

    if(!packetSent) {
        packetSentStamp = millisNow;
        while (_udp->parsePacket() != 0) {
            _udp->flush();
        }
        sendNTPPacket();
        packetSent = true;
    }
    
	int responseBytes = _udp->parsePacket();
	if (responseBytes == 0) {
		uint32_t elapsed = getMillisSpan(packetSentStamp, millisNow);
		if (elapsed >= MAX_ACCEPTABLE_DELAY) {
			NTP_LOG_ERROR_MSG("timeout, fail count=%d\n", _failCount);
			_updateTriggered = false;
			_failCount++;
			packetSent = false;
            if (_failCount > 9999) _failCount = 9999;
            if (_callbackUpdate != NULL) {
                NtpTime data;
                data.time = 0;
                data.success = false;
                _callbackUpdate(data);
            }
			return false;
		}
		return true;
	}

	packetReceivedStamp = millisNow;
	_updateTriggered = false;
	packetSent = false;
	_failCount = 0;
	_lastBackoffInterval = NTP_FAIL_MIN_WAIT_INTERVAL;

	NTP_LOG_DEBUG_MSG("got data back\n");

	_udp->read(_packetBuffer, NTP_PACKET_SIZE);

	// Kiss of Death (KoD) detection: stratum == 0
	if (_packetBuffer[1] == 0) {
		char kodCode[5];
		kodCode[0] = (char)_packetBuffer[12];
		kodCode[1] = (char)_packetBuffer[13];
		kodCode[2] = (char)_packetBuffer[14];
		kodCode[3] = (char)_packetBuffer[15];
		kodCode[4] = '\0';
		NTP_LOG_ERROR_MSG("Kiss of Death received: code '%s'\n", kodCode);
		_updateTriggered = false;
		_failCount++;
		packetSent = false;
		if (_failCount > 9999) _failCount = 9999;
		if (_callbackUpdate != NULL) {
			NtpTime data;
			data.time = 0;
			data.success = false;
			_callbackUpdate(data);
		}
		return false;
	}

    unsigned long highWord = word(_packetBuffer[40], _packetBuffer[41]);
    unsigned long lowWord = word(_packetBuffer[42], _packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    uint64_t expectedTime = getTime();
	uint64_t newNtpTime = (uint64_t)secsSince1900 * 1000ULL;
	if (secsSince1900 < NTP_ROLLOVER_PIVOT) {
		NTP_LOG_INFO_MSG("rollover detected\n");
		newNtpTime += 4294967296000ULL;
	}
	else {
		NTP_LOG_DEBUG_MSG("no rollover\n");
	}

    highWord = word(_packetBuffer[44], _packetBuffer[45]);
    lowWord = word(_packetBuffer[46], _packetBuffer[47]);
	uint32_t fracRaw = highWord << 16 | lowWord;
	fracRaw = fracRaw / 4294968UL;
    _lastNtpDelay = getMillisSpan(packetSentStamp, packetReceivedStamp) / 2;

	//guard against unreasonable round trip times (RTT) that would throw off our calculations
	//if the delay is too high, we can assume the time is not accurate and ignore the sample (but still reset the backoff since we did get a response)
	if (_lastNtpDelay > MAX_ACCEPTABLE_DELAY / 2) {
		NTP_LOG_ERROR_MSG("unreasonable NTP delay: %lu ms\n", _lastNtpDelay);
		_failCount = 0; // Clear fail count since we did get a response, even if it's not usable
		return false;
	}

	newNtpTime = newNtpTime - YEAR1970STAMP_MS + (uint64_t)(fracRaw + _lastNtpDelay);
	uint64_t ntpElapsed = newNtpTime - _lastNtpTimeReceived;
	_lastNtpTimeReceived = newNtpTime;
	_lastLocalMillisNtpReceived = millisNow; // Track when NTP packet was received (for getNTPAge)

	if (_state == NtpState::INIT) {
        _ntpTime = newNtpTime;
        _accumulatedReferenceMillis = 0;
        _lastReferenceMillis = millisNow;
        _lastLocalMillisUpdate = millisNow;
        _isStale = false;
        _neverUpdated = false;
        averagingIndex = 2;
        setMachineState(NtpState::SYNCED);
        NTP_LOG_INFO_MSG("first sync, UTC time = %llu.%03u\n", newNtpTime / 1000ULL, (unsigned int)(newNtpTime % 1000ULL));

        if (_callbackUpdate != NULL) {
            NtpTime data;
            data.time = newNtpTime;
            data.success = true;
            _callbackUpdate(data);
        }
        return true;
    }

	if (_state == NtpState::SYNCED) {
		uint32_t localElapsed = getMillisSpan(_lastLocalMillisUpdate, millisNow);

		if (_skipNextDriftCalc) {
			// After setTime(): drift calculation is invalid, but offset handling still applies
			_skipNextDriftCalc = false;
			NTP_LOG_DEBUG_MSG("skipping drift calculation after setTime()\n");

			int64_t clockOffset = (int64_t)(expectedTime - (int64_t)newNtpTime);
			_lastClockOffset = clockOffset;
			NTP_LOG_DEBUG_MSG("clock offset = %lld ms (local-ntp)\n", clockOffset);

			int64_t absOffset = clockOffset < 0 ? -clockOffset : clockOffset;
			uint32_t instantThresholdMs = (uint32_t)_instantOffsetCorrection * 1000UL;

			if (absOffset >= (int64_t)instantThresholdMs) {
				NTP_LOG_INFO_MSG("offset %lld ms exceeds instant threshold %lu ms, correcting immediately\n", clockOffset, instantThresholdMs);
				_ntpTime = newNtpTime;
				_accumulatedReferenceMillis = 0;
				_lastReferenceMillis = millisNow;
			}
			else if (absOffset > (int64_t)_lastNtpDelay) {
				// After setTime(), any offset beyond NTP jitter should be corrected
				// promptly via catch-up rather than waiting an entire update interval
				_userDrift = _baseDrift;
				anchorTime();
				uint32_t catchUpIntervalMs = (uint32_t)_catchUpCorrectionInterval * 1000U;

				if (clockOffset > 0) {
					NTP_LOG_INFO_MSG("local clock ahead by %lld ms, entering CATCHING_UP (slow down)\n", clockOffset);
					setCatchUpDrift((float)absOffset * 1000000.0f / (float)catchUpIntervalMs + _userDrift);
				} else {
					NTP_LOG_INFO_MSG("local clock behind by %lld ms, entering CATCHING_UP (speed up)\n", absOffset);
					setCatchUpDrift(-(float)absOffset * 1000000.0f / (float)catchUpIntervalMs + _userDrift);
				}

				NTP_LOG_DEBUG_MSG("catch-up drift = %.2f ppm over %lu ms\n", _catchUpDrift, catchUpIntervalMs);
				setMachineState(NtpState::CATCHING_UP);
			}

			// Re-anchor for valid drift calculation on subsequent updates
			_lastLocalMillisUpdate = millisNow;
		}
		else {
			averagingIndex++;
			if (averagingIndex > 10) averagingIndex = 10;

			if (localElapsed < 1000) {
				NTP_LOG_DEBUG_MSG("localElapsed too small, skipping drift calculation\n");
			}
			else {
				NTP_LOG_DEBUG_MSG("calculate drift: NTP elapsed=%" PRIu64 "ms, local elapsed=%lu ms", ntpElapsed, localElapsed);
				float periodDrift = ((float)((int64_t)localElapsed - (int64_t)ntpElapsed) * 1000000.0f / (float)ntpElapsed);
				NTP_LOG_DEBUG_MSG(" | net=%lld ms over %lu ms\n", (int64_t)localElapsed - (int64_t)ntpElapsed, ntpElapsed);

				// Smooth the BASE drift only (without offset compensation)
				float smoothedBaseDrift = _baseDrift * (1.0f - 1.0f/(float)averagingIndex) + periodDrift * 1.0f/(float)averagingIndex;

				int64_t clockOffset = (int64_t)(expectedTime - (int64_t)newNtpTime);
				_lastClockOffset = clockOffset;
				NTP_LOG_DEBUG_MSG("clock offset = %lld ms (local-ntp)\n", clockOffset);

				int64_t absOffset = clockOffset < 0 ? -clockOffset : clockOffset;
				uint32_t instantThresholdMs = (uint32_t)_instantOffsetCorrection * 1000UL;

				if (absOffset >= (int64_t)instantThresholdMs) {
					NTP_LOG_INFO_MSG("offset %lld ms exceeds instant threshold %lu ms, correcting immediately\n", clockOffset, instantThresholdMs);
					_ntpTime = newNtpTime;
					_accumulatedReferenceMillis = 0;
					_lastReferenceMillis = millisNow;
					_lastLocalMillisUpdate = millisNow;
					_isStale = false;
					_baseDrift = _userDrift;
					_computedClockDrift = _userDrift;
				}
				else if (absOffset > CATCHUP_THRESHOLD) {
					_baseDrift = smoothedBaseDrift;
					if (_baseDrift > 200000.0f) _baseDrift = 200000.0f;
					if (_baseDrift < -200000.0f) _baseDrift = -200000.0f;
					_userDrift = _baseDrift;
					anchorTime();
					uint32_t catchUpIntervalMs = (uint32_t)_catchUpCorrectionInterval * 1000U;

					if (clockOffset > 0) {
						NTP_LOG_INFO_MSG("local clock ahead by %lld ms, entering CATCHING_UP (slow down)\n", clockOffset);
						setCatchUpDrift((float)absOffset * 1000000.0f / (float)catchUpIntervalMs + _userDrift);
					} else {
						NTP_LOG_INFO_MSG("local clock behind by %lld ms, entering CATCHING_UP (speed up)\n", absOffset);
						setCatchUpDrift(-(float)absOffset * 1000000.0f / (float)catchUpIntervalMs + _userDrift);
					}

					NTP_LOG_DEBUG_MSG("catch-up drift = %.2f ppm over %lu ms\n", _catchUpDrift, catchUpIntervalMs);
					_lastLocalMillisUpdate = millisNow;
					setMachineState(NtpState::CATCHING_UP);
				}
				else {
					// Update base drift
					_baseDrift = smoothedBaseDrift;

					// Calculate fresh offset compensation based on running offset, smooth value, target catching up within 4 update intervals
					_runningOffset = (_runningOffset * 80L + (int32_t)clockOffset * 20L) / 100L;
					float offsetCompensation = ((float)_runningOffset * 1000000.0f / (float)_updateInterval) * 0.25f;

					// Apply total drift = base drift + offset compensation
					anchorTime();
					_computedClockDrift = _baseDrift + offsetCompensation;
					if (_computedClockDrift > 200000.0f) _computedClockDrift = 200000.0f;
					if (_computedClockDrift < -200000.0f) _computedClockDrift = -200000.0f;

					NTP_LOG_DEBUG_MSG("base drift = %.2f ppm, offset compensation = %.2f ppm, total drift = %.2f ppm\n", 
						_baseDrift, offsetCompensation, _computedClockDrift);
					_lastLocalMillisUpdate = millisNow;
				}
			}
		}
	}

	NTP_LOG_DEBUG_MSG("UTC time = %" PRIu64 ".%03u\n", newNtpTime / 1000ULL, (unsigned int)(newNtpTime % 1000ULL));

	if (_state == NtpState::SYNCED) {
        _isStale = false;
    }

    if (_callbackUpdate != NULL) {
        NtpTime data;
        data.time = newNtpTime;
        data.success = true;
        _callbackUpdate(data);
    }

    return true;
}

int64_t NtpClient::getLastClockOffset() const {
	return _lastClockOffset;
}

uint64_t NtpClient::getLastNtpUpdateTime() const {
	return _lastNtpTimeReceived;
}

uint32_t NtpClient::getLastNtpDelay() const {
    return _lastNtpDelay;
}

void NtpClient::setCatchUpDrift(float ppm) {
    _catchUpDrift = ppm;
    if(_catchUpDrift > 200000.0f) _catchUpDrift = 200000.0f;
    if(_catchUpDrift < -500000.0f) _catchUpDrift = -500000.0f;
}
