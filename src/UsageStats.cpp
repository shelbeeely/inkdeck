#include "UsageStats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace usage {

namespace {

// CSV field: quoted when it holds a comma, quote or line break.
size_t field(const char* s, char* out, size_t outsz) {
  const bool quote = strpbrk(s, ",\"\r\n") != nullptr;
  size_t o = 0;
  if (quote && o + 1 < outsz) out[o++] = '"';
  for (; *s && o + 2 < outsz; ++s) {
    if (*s == '\r' || *s == '\n') continue;
    if (*s == '"') out[o++] = '"';
    out[o++] = *s;
  }
  if (quote && o + 1 < outsz) out[o++] = '"';
  out[o] = '\0';
  return o;
}

// Next CSV field of *p into out; advances *p past the comma.
void next(const char*& p, char* out, size_t outsz) {
  size_t o = 0;
  if (*p == '"') {
    ++p;
    while (*p) {
      if (*p == '"' && p[1] == '"') {
        if (o + 1 < outsz) out[o++] = '"';
        p += 2;
      } else if (*p == '"') {
        ++p;
        break;
      } else {
        if (o + 1 < outsz) out[o++] = *p;
        ++p;
      }
    }
    while (*p && *p != ',') ++p;
  } else {
    while (*p && *p != ',' && *p != '\r' && *p != '\n') {
      if (o + 1 < outsz) out[o++] = *p;
      ++p;
    }
  }
  if (*p == ',') ++p;
  out[o] = '\0';
}

// "YYYY-MM-DDTHH..." -> day number and hour; false if not a timestamp.
bool dayHour(const char* iso, int32_t& day, int& hour) {
  int y, m, d, h;
  if (sscanf(iso, "%4d-%2d-%2dT%2d", &y, &m, &d, &h) != 4) return false;
  day = daysFromCivil(y, m, d);
  hour = h;
  return true;
}

}  // namespace

size_t format(const Event& e, char* out, size_t outsz) {
  char path[340], detail[200], battery[8];
  field(e.path, path, sizeof(path));
  field(e.detail, detail, sizeof(detail));
  if (e.battery >= 0) snprintf(battery, sizeof(battery), "%d", e.battery);
  else battery[0] = '\0';
  const int n = snprintf(out, outsz, "%s,%s,%s,%lu,%lu,%lu,%lu,%lu,%ld,%lu,%s,%s", e.time, e.event, path,
                         static_cast<unsigned long>(e.durationS), static_cast<unsigned long>(e.activeS),
                         static_cast<unsigned long>(e.keys), static_cast<unsigned long>(e.typed),
                         static_cast<unsigned long>(e.deleted), static_cast<long>(e.words),
                         static_cast<unsigned long>(e.wpm), battery, detail);
  return n > 0 && static_cast<size_t>(n) < outsz ? static_cast<size_t>(n) : 0;
}

bool parse(const char* line, Event& e) {
  e = Event();
  const char* p = line;
  char f[24];
  next(p, e.time, sizeof(e.time));
  if (strcmp(e.time, "time") == 0 || !e.time[0]) return false;
  next(p, e.event, sizeof(e.event));
  next(p, e.path, sizeof(e.path));
  next(p, f, sizeof(f)); e.durationS = strtoul(f, nullptr, 10);
  next(p, f, sizeof(f)); e.activeS = strtoul(f, nullptr, 10);
  next(p, f, sizeof(f)); e.keys = strtoul(f, nullptr, 10);
  next(p, f, sizeof(f)); e.typed = strtoul(f, nullptr, 10);
  next(p, f, sizeof(f)); e.deleted = strtoul(f, nullptr, 10);
  next(p, f, sizeof(f)); e.words = strtol(f, nullptr, 10);
  next(p, f, sizeof(f)); e.wpm = strtoul(f, nullptr, 10);
  next(p, f, sizeof(f)); e.battery = f[0] ? atoi(f) : -1;
  next(p, e.detail, sizeof(e.detail));
  return e.event[0] != '\0';
}

// Howard Hinnant's civil-date algorithms.
int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe) + era * 400 + (m <= 2);
}

void Stats::add(const Event& e) {
  if (strcmp(e.event, "sync") == 0) {
    ++syncs;
    if (!strstr(e.detail, " ok")) ++syncsFailed;
    return;
  }
  if (strcmp(e.event, "keyboard") == 0) {
    if (strncmp(e.detail, "disconnected", 12) == 0) keyboardS += e.durationS;
    return;
  }
  if (strcmp(e.event, "session") != 0) return;
  int32_t day;
  int hour;
  if (!dayHour(e.time, day, hour)) return;

  ++sessions;
  if (strcmp(e.detail, "journal") == 0) ++journalSessions;
  activeS += e.activeS;
  words += e.words;
  typed += e.typed;
  deleted += e.deleted;
  if (today - day < 30 && hour >= 0 && hour < 24) hourActiveS[hour] += e.activeS;

  Day* slot = nullptr;
  for (int i = dayCount - 1; i >= 0; --i) {  // logs are mostly in order: search from the end
    if (days[i].day == day) {
      slot = &days[i];
      break;
    }
  }
  if (!slot) {
    if (dayCount == kMaxDays) {  // keep the most recent days
      memmove(days, days + 1, sizeof(Day) * (kMaxDays - 1));
      --dayCount;
    }
    slot = &days[dayCount++];
    *slot = Day();
    slot->day = day;
  }
  slot->words += e.words;
  slot->activeS += e.activeS;
  ++slot->sessions;
}

Summary summarize(const Stats& s, int32_t today) {
  Summary r;
  // Monday-based week: 1970-01-01 was a Thursday.
  const int weekday = static_cast<int>(((today % 7) + 7 + 3) % 7);  // 0 = Monday
  const int32_t weekStart = today - weekday;
  int y, m, d;
  civilFromDays(today, y, m, d);
  const int32_t monthStart = daysFromCivil(y, m, 1);

  for (int i = 0; i < s.dayCount; ++i) {
    const Stats::Day& day = s.days[i];
    if (day.day == today) {
      r.todayWords += day.words;
      r.todayActiveS += day.activeS;
      r.todaySessions += day.sessions;
    }
    if (day.day >= weekStart && day.day <= today) {
      r.weekWords += day.words;
      r.weekActiveS += day.activeS;
      r.weekSessions += day.sessions;
    }
    if (day.day >= monthStart && day.day <= today) {
      r.monthWords += day.words;
      r.monthActiveS += day.activeS;
      r.monthSessions += day.sessions;
    }
    if (day.words > r.bestDayWords) {
      r.bestDayWords = day.words;
      r.bestDay = day.day;
    }
  }

  // Streaks over days with any writing (sorted copy of the day numbers).
  static int32_t sorted[Stats::kMaxDays];
  int n = 0;
  for (int i = 0; i < s.dayCount; ++i) {
    if (s.days[i].activeS > 0 || s.days[i].words > 0) sorted[n++] = s.days[i].day;
  }
  for (int i = 1; i < n; ++i) {  // insertion sort: n is small and nearly sorted
    const int32_t v = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > v) {
      sorted[j + 1] = sorted[j];
      --j;
    }
    sorted[j + 1] = v;
  }
  uint32_t run = 0;
  for (int i = 0; i < n; ++i) {
    run = (i > 0 && sorted[i] == sorted[i - 1] + 1) ? run + 1 : 1;
    if (run > r.bestStreak) r.bestStreak = run;
  }
  // The current streak counts if it reaches today or yesterday.
  if (n > 0 && sorted[n - 1] >= today - 1) {
    r.streak = 1;
    for (int i = n - 1; i > 0 && sorted[i] == sorted[i - 1] + 1; --i) ++r.streak;
  }

  if (s.sessions) r.avgSessionS = s.activeS / s.sessions;
  if (s.activeS >= 30 && s.words > 0) r.avgWpm = static_cast<uint32_t>(s.words * 60 / s.activeS);
  if (s.typed) r.deletedPct = static_cast<uint32_t>(s.deleted * 100 / s.typed);
  return r;
}

}  // namespace usage
