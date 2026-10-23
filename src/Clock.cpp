#include "Clock.h"

#include <Preferences.h>
#include <esp_sntp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

namespace Clock {

namespace {
constexpr const char* kPrefsNs = "inkdeck";
constexpr const char* kLastTimeKey = "lasttime";
constexpr time_t kMinValid = 1704067200;  // 2024-01-01

bool localNow(struct tm& out) {
  if (!valid()) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}
}  // namespace

void applyTimezone(const char* posixTz) {
  setenv("TZ", posixTz && posixTz[0] ? posixTz : "UTC0", 1);
  tzset();
}

bool valid() { return time(nullptr) >= kMinValid; }

static volatile bool g_ntpSynced = false;
static void onNtpSync(struct timeval*) { g_ntpSynced = true; }

bool syncNtp(uint32_t timeoutMs) {
  // The clock may already look valid (it survives deep sleep), so wait for the
  // server's answer rather than for a plausible year.
  g_ntpSynced = false;
  sntp_set_time_sync_notification_cb(onNtpSync);
  const char* tz = getenv("TZ");
  configTzTime(tz ? tz : "UTC0", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (g_ntpSynced && valid()) {
      rememberNow();
      return true;
    }
    delay(100);
  }
  return false;
}

bool setLocal(int year, int month, int day, int hour, int minute) {
  if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59) {
    return false;
  }
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_isdst = -1;
  const time_t when = mktime(&t);
  if (when < kMinValid || t.tm_mday != day) return false;  // mktime normalized e.g. Feb 31
  struct timeval tv = {when, 0};
  settimeofday(&tv, nullptr);
  rememberNow();
  return true;
}

bool setFromString(const char* s) {
  int y = 0, mo = 0, d = 0, h = 12, mi = 0;
  const int n = sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
  if (n != 3 && n != 5) return false;
  return setLocal(y, mo, d, h, mi);
}

void rememberNow() {
  if (!valid()) return;
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) return;
  prefs.putLong64(kLastTimeKey, static_cast<int64_t>(time(nullptr)));
  prefs.end();
}

void formatForEntry(char* out, size_t outsz) {
  out[0] = '\0';
  time_t when = 0;
  if (valid()) {
    when = time(nullptr);
  } else {
    Preferences prefs;
    if (prefs.begin(kPrefsNs, true)) {
      when = static_cast<time_t>(prefs.getLong64(kLastTimeKey, 0));
      prefs.end();
    }
    if (when < kMinValid) return;
  }
  struct tm t;
  localtime_r(&when, &t);
  strftime(out, outsz, "%Y-%m-%d %H:%M", &t);
}

void formatDate(char* out, size_t outsz) {
  struct tm t;
  if (!localNow(t)) {
    out[0] = '\0';
    return;
  }
  strftime(out, outsz, "%Y-%m-%d", &t);
}

void formatTime(char* out, size_t outsz) {
  struct tm t;
  if (!localNow(t)) {
    out[0] = '\0';
    return;
  }
  strftime(out, outsz, "%H:%M", &t);
}

void formatIso(char* out, size_t outsz) {
  if (!valid()) {
    out[0] = '\0';
    return;
  }
  formatIsoAt(time(nullptr), out, outsz);
}

void formatClockAt(int64_t when, char* out, size_t outsz) {
  const time_t tt = static_cast<time_t>(when);
  struct tm t;
  localtime_r(&tt, &t);
  strftime(out, outsz, "%H:%M:%S", &t);
}

void formatIsoAt(int64_t when, char* out, size_t outsz) {
  const time_t tt = static_cast<time_t>(when);
  struct tm t;
  localtime_r(&tt, &t);
  // strftime's %z gives "+HHMM"; GitJournal writes "+HH:MM".
  char base[24];
  char zone[8];
  strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &t);
  strftime(zone, sizeof(zone), "%z", &t);
  if (strlen(zone) == 5) {
    snprintf(out, outsz, "%s%.3s:%.2s", base, zone, zone + 3);
  } else {
    snprintf(out, outsz, "%sZ", base);
  }
}

}  // namespace Clock
