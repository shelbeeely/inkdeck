#pragma once

// Wi-Fi sessions. The C3 has one 2.4 GHz radio; Wi-Fi and BLE can time-share
// it, but not with enough RAM left for TLS, so a session pauses the BLE host
// (freeing its heap) and brings it back when Wi-Fi goes down.

#include <Arduino.h>

namespace Net {

// Pauses BLE and joins `ssid`, waiting up to timeoutMs. On failure Wi-Fi is
// already off again, but BLE stays paused until down() is called.
bool up(const char* ssid, const char* password, uint32_t timeoutMs = 20000);
// Turns Wi-Fi off and restarts BLE.
void down();

}  // namespace Net
