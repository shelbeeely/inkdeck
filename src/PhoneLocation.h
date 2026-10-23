#pragma once

// Location of a journal session, read from the user's phone over Bluetooth LE
// (docs/phone-location.md). The phone runs a GATT server advertising the
// InkDeck location service; the X4 (already a BLE central for the keyboard)
// scans for it for a few seconds, connects, reads one characteristic and
// disconnects, all on a background task so typing isn't held up.

#include <Arduino.h>

#include "JournalMeta.h"

namespace PhoneLocation {

// UUIDs the phone's GATT server must use.
constexpr const char* kServiceUuid = "ff3f25e1-53df-4bf1-9794-eac66727467a";
constexpr const char* kLocationUuid = "ff3f0001-53df-4bf1-9794-eac66727467a";

// For a new session: starts a lookup if location = phone and the last one is
// older than location_every minutes; otherwise the last fix is reused.
void request();
// A fix for the session, once available (true once per request).
bool take(journalmeta::Fix& out);
// Waits for a running lookup to finish (before Bluetooth is shut down).
void waitIdle(uint32_t timeoutMs);

}  // namespace PhoneLocation
