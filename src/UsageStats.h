#pragma once

// Usage log format and the aggregation behind the Stats screen. Pure (no
// Arduino/SD dependencies), host-testable.
//
// The log is CSV, one file per month (<notes>/_inkdeck/usage/YYYY-MM.csv),
// synced with the journal so the dashboard can read it too:
//
//   time,event,path,duration_s,active_s,keys,typed,deleted,words,wpm,battery,detail
//   2026-10-23T14:51:40-07:00,session,Journal/2026-10-23.md,1220,912,2710,2390,270,433,28,81,journal
//
// Events: boot (detail cold|wake), sleep, session (one editing session of any
// file; detail journal|note), create, rename (detail = new path), delete,
// sync (detail "down=3 up=2 deleted=0 conflicts=0 ok"), keyboard (detail
// "connected <name>" | "disconnected"; duration_s = time connected).
// battery is a percentage, empty when unknown.

#include <stddef.h>
#include <stdint.h>

namespace usage {

constexpr const char* kHeader = "time,event,path,duration_s,active_s,keys,typed,deleted,words,wpm,battery,detail";

struct Event {
  char time[32] = {0};  // local ISO 8601 with offset
  char event[12] = {0};
  char path[160] = {0};
  uint32_t durationS = 0;
  uint32_t activeS = 0;
  uint32_t keys = 0;
  uint32_t typed = 0;
  uint32_t deleted = 0;
  int32_t words = 0;
  uint32_t wpm = 0;
  int battery = -1;  // -1 = unknown
  char detail[96] = {0};
};

// One CSV line, without the newline. Returns the length (0 if it doesn't fit).
size_t format(const Event& e, char* out, size_t outsz);
// Parses a line written by format(); false for the header or malformed lines.
bool parse(const char* line, Event& e);

// Days since 1970-01-01 for a civil date, and back.
int32_t daysFromCivil(int y, int m, int d);
void civilFromDays(int32_t z, int& y, int& m, int& d);

// Totals for the Stats screen, fed with every logged event.
struct Stats {
  static constexpr int kMaxDays = 400;
  struct Day {
    int32_t day = 0;  // daysFromCivil
    int32_t words = 0;
    uint32_t activeS = 0;
    uint16_t sessions = 0;
  };
  Day days[kMaxDays];
  int dayCount = 0;
  uint32_t hourActiveS[24] = {0};  // last 30 days, by local hour
  uint32_t sessions = 0;
  uint32_t activeS = 0;
  int64_t words = 0;
  uint64_t typed = 0;
  uint64_t deleted = 0;
  uint32_t journalSessions = 0;
  uint32_t syncs = 0;
  uint32_t syncsFailed = 0;
  uint32_t keyboardS = 0;  // time a keyboard was connected
  int32_t today = 0;       // set before adding events (for the 30-day window)

  void add(const Event& e);
};

// Read-outs over Stats for a given day (daysFromCivil of "today").
struct Summary {
  int32_t todayWords = 0, weekWords = 0, monthWords = 0;
  uint32_t todayActiveS = 0, weekActiveS = 0, monthActiveS = 0;
  uint32_t todaySessions = 0, weekSessions = 0, monthSessions = 0;
  uint32_t streak = 0;      // consecutive writing days ending today (or yesterday)
  uint32_t bestStreak = 0;
  int32_t bestDay = 0;      // daysFromCivil, 0 = none
  int32_t bestDayWords = 0;
  uint32_t avgSessionS = 0;
  uint32_t avgWpm = 0;
  uint32_t deletedPct = 0;  // deleted / typed
};
Summary summarize(const Stats& s, int32_t today);

}  // namespace usage
