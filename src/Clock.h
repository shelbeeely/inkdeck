#pragma once

// Wall clock for journaling. The X4 has no RTC chip, but the ESP32-C3 keeps
// system time counting through deep sleep, so a time set by NTP (during Sync)
// or by hand survives sleep/wake. It is lost on reset or a flat battery; the
// last known time is kept in NVS to prefill the manual Set date/time entry.

#include <Arduino.h>
#include <time.h>

namespace Clock {

void applyTimezone(const char* posixTz);  // call at boot (TZ is lost on wake)
bool valid();                             // true once the year is plausible

// NTP over the current Wi-Fi connection; blocks up to timeoutMs.
bool syncNtp(uint32_t timeoutMs);

// Sets the local time; returns false if the fields are out of range.
bool setLocal(int year, int month, int day, int hour, int minute);
// Parses "YYYY-MM-DD HH:MM" (time optional) and sets the local time.
bool setFromString(const char* s);

void rememberNow();  // persist the current time as the last known time
// Writes "YYYY-MM-DD HH:MM" for now, or the last known time when the clock
// isn't set (empty if never set).
void formatForEntry(char* out, size_t outsz);

void formatDate(char* out, size_t outsz);  // "YYYY-MM-DD"
void formatTime(char* out, size_t outsz);  // "HH:MM"
void formatIso(char* out, size_t outsz);   // "YYYY-MM-DDTHH:MM:SS+HH:MM" (GitJournal)
// The same for a given unix time, and "HH:MM:SS" (for journal metadata).
void formatIsoAt(int64_t t, char* out, size_t outsz);
void formatClockAt(int64_t t, char* out, size_t outsz);

}  // namespace Clock
