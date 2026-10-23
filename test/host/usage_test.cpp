#include "UsageStats.h"
#include "JournalMeta.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace usage;
static int fails = 0;
#define CHECK(name, cond) do { bool ok = (cond); printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) fails++; } while (0)
static std::string line(const char* time, const char* path, uint32_t active, int32_t words, const char* detail = "journal") {
  Event e; strcpy(e.time, time); strcpy(e.event, "session"); strcpy(e.path, path); e.durationS = active + 60; e.activeS = active;
  e.keys = words * 6; e.typed = words * 6; e.deleted = words; e.words = words; e.wpm = 30; e.battery = 80; strcpy(e.detail, detail);
  char out[512]; size_t n = format(e, out, sizeof out); return std::string(out, n);
}
int main() {
  // Round trip with awkward fields.
  Event e; strcpy(e.time, "2026-10-23T14:51:40-07:00"); strcpy(e.event, "rename"); strcpy(e.path, "Ideas/a, \"b\".md");
  strcpy(e.detail, "Ideas/new, name.md"); e.battery = -1;
  char out[512]; format(e, out, sizeof out); printf("  %s\n", out);
  Event back; CHECK("round trip", parse(out, back) && !strcmp(back.path, e.path) && !strcmp(back.detail, e.detail) && back.battery == -1);
  CHECK("header skipped", !parse(kHeader, back));
  // Dates.
  int y, m, d; civilFromDays(daysFromCivil(2026, 10, 23), y, m, d);
  CHECK("civil round trip", y == 2026 && m == 10 && d == 23);
  CHECK("known epoch", daysFromCivil(1970, 1, 1) == 0 && daysFromCivil(2000, 3, 1) == 11017);
  // Aggregation: Thu 2026-10-22 and Fri 10-23 (today), plus a 3-day run in early October.
  Stats st; const int32_t today = daysFromCivil(2026, 10, 23); st.today = today;
  const char* lines[] = {};
  (void)lines;
  std::string L[] = {
    line("2026-10-02T08:00:00-07:00", "J/2026-10-02.md", 600, 300),
    line("2026-10-03T08:00:00-07:00", "J/2026-10-03.md", 600, 300),
    line("2026-10-04T21:00:00-07:00", "J/2026-10-04.md", 1200, 900),
    line("2026-10-19T07:30:00-07:00", "J/2026-10-19.md", 300, 120),   // Monday: this week
    line("2026-10-22T07:30:00-07:00", "J/2026-10-22.md", 900, 400),
    line("2026-10-23T07:10:00-07:00", "J/2026-10-23.md", 600, 250),
    line("2026-10-23T22:15:00-07:00", "Ideas/x.md", 300, 50, "note"),
  };
  for (auto& s : L) { Event ev; if (parse(s.c_str(), ev)) st.add(ev); }
  Event sync; parse("2026-10-23T09:00:00-07:00,sync,,12,0,0,0,0,0,0,80,down=1 up=2 deleted=0 conflicts=0 ok", sync); st.add(sync);
  Event kb; parse("2026-10-23T10:00:00-07:00,keyboard,,3600,0,0,0,0,0,0,,disconnected", kb); st.add(kb);
  Summary s = summarize(st, today);
  printf("  today %d w %us %u sess | week %d w | month %d w | streak %u best %u | best day words %d | avg %us %u wpm | deleted %u%%\n",
         s.todayWords, s.todayActiveS, s.todaySessions, s.weekWords, s.monthWords, s.streak, s.bestStreak, s.bestDayWords,
         s.avgSessionS, s.avgWpm, s.deletedPct);
  CHECK("today", s.todayWords == 300 && s.todaySessions == 2 && s.todayActiveS == 900);
  CHECK("week from Monday", s.weekWords == 120 + 400 + 300);
  CHECK("month", s.monthWords == 300 + 300 + 900 + 120 + 400 + 300);
  CHECK("current streak (22nd-23rd)", s.streak == 2);
  CHECK("best streak (2nd-4th)", s.bestStreak == 3);
  CHECK("best day", s.bestDayWords == 900 && s.bestDay == daysFromCivil(2026, 10, 4));
  CHECK("hour buckets", st.hourActiveS[7] == 300 + 900 + 600 && st.hourActiveS[22] == 300);
  CHECK("syncs and keyboard", st.syncs == 1 && st.syncsFailed == 0 && st.keyboardS == 3600);
  CHECK("journal sessions", st.journalSessions == 6);
  // Counters: pauses, bursts, first key, wpm.
  journalmeta::Counters c; c.opened = 1000;
  uint32_t ms = 0; int64_t now = 1004;
  c.edit(ms, now, 1, 0, false);
  for (int i = 0; i < 20; ++i) { ms += 300; c.edit(ms, now, 1, 0, false); }   // 6 s burst
  ms += 90000; c.edit(ms, now + 96, 1, 0, false);                             // 90 s pause
  ms += 300; c.edit(ms, now + 96, 0, 3, true);                                // a revision deleting 3
  CHECK("counters", c.keys == 23 && c.typed == 22 && c.deleted == 3 && c.pauses == 1 && c.longestPauseMs == 90000 &&
                    c.longestBurstMs == 6000 && c.firstKey - c.opened == 4 && c.revisions == 1 && c.activeMs == 6300);
  CHECK("wpm", journalmeta::wordsPerMinute(100, 120000) == 50 && journalmeta::wordsPerMinute(5, 10000) == 0);
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails != 0;
}
