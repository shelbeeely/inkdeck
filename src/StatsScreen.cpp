#include <stdio.h>

#include "UsageLog.h"
#include "screens.h"

// The Stats screen: totals and patterns from the usage log (UsageStats.h).

using namespace freeink::ui;

namespace {
usage::Stats* g_stats = nullptr;  // ~7 KB, only while the screen is up
usage::Summary g_summary;
}  // namespace

bool statsLoad() {
  if (!g_stats) g_stats = new usage::Stats();
  int32_t today = 0;
  if (!g_stats || !UsageLog::load(*g_stats, today)) {
    statsFree();
    return false;
  }
  g_summary = usage::summarize(*g_stats, today);
  return true;
}

void statsFree() {
  delete g_stats;
  g_stats = nullptr;
}

// "1 h 05 min" / "18 min" / "40 s"
static void formatDuration(uint32_t s, char* out, size_t outsz) {
  if (s >= 3600) snprintf(out, outsz, "%lu h %02lu min", static_cast<unsigned long>(s / 3600),
                          static_cast<unsigned long>(s % 3600 / 60));
  else if (s >= 60) snprintf(out, outsz, "%lu min", static_cast<unsigned long>(s / 60));
  else snprintf(out, outsz, "%lu s", static_cast<unsigned long>(s));
}

void statsScreen(ScreenT& s, void*) {
  s.header("Stats", nullptr, "");
  uiDrawHardwareFooter("Back", "", "", "");
  if (!g_stats) return;
  const usage::Summary& m = g_summary;
  const usage::Stats& st = *g_stats;

  Rect body = s.body();
  auto& target = s.frame().target();
  TextStyle label = g_theme.smallText;
  label.maxLines = 1;
  TextStyle value = g_theme.bodyText;
  value.maxLines = 1;
  const int16_t lineH = static_cast<int16_t>(target.lineHeight(value.font) + 10);
  const int16_t labelW = 128;
  int16_t y = static_cast<int16_t>(body.y + 12);
  char a[24], b[24], line[96];

  auto row = [&](const char* name, const char* text) {
    target.text(Rect{static_cast<int16_t>(body.x + 4), static_cast<int16_t>(y + 3), labelW, lineH}, name, label);
    target.text(Rect{static_cast<int16_t>(body.x + labelW), y, static_cast<int16_t>(body.width - labelW), lineH}, text, value);
    y = static_cast<int16_t>(y + lineH);
  };

  formatDuration(m.todayActiveS, a, sizeof(a));
  snprintf(line, sizeof(line), "%ld words, %s, %lu session%s", static_cast<long>(m.todayWords), a,
           static_cast<unsigned long>(m.todaySessions), m.todaySessions == 1 ? "" : "s");
  row("Today", line);
  formatDuration(m.weekActiveS, a, sizeof(a));
  snprintf(line, sizeof(line), "%ld words, %s", static_cast<long>(m.weekWords), a);
  row("Week", line);
  formatDuration(m.monthActiveS, a, sizeof(a));
  snprintf(line, sizeof(line), "%ld words, %s", static_cast<long>(m.monthWords), a);
  row("Month", line);
  snprintf(line, sizeof(line), "%lu day%s (best %lu)", static_cast<unsigned long>(m.streak), m.streak == 1 ? "" : "s",
           static_cast<unsigned long>(m.bestStreak));
  row("Streak", line);
  if (m.bestDay) {
    int yy, mm, dd;
    usage::civilFromDays(m.bestDay, yy, mm, dd);
    snprintf(line, sizeof(line), "%04d-%02d-%02d, %ld words", yy, mm, dd, static_cast<long>(m.bestDayWords));
  } else {
    snprintf(line, sizeof(line), "-");
  }
  row("Best day", line);
  formatDuration(m.avgSessionS, a, sizeof(a));
  snprintf(line, sizeof(line), "%s a session, %lu wpm", a, static_cast<unsigned long>(m.avgWpm));
  row("Average", line);
  snprintf(line, sizeof(line), "%lu%% of typing deleted", static_cast<unsigned long>(m.deletedPct));
  row("Editing", line);
  snprintf(line, sizeof(line), "%lu (%lu failed)", static_cast<unsigned long>(st.syncs),
           static_cast<unsigned long>(st.syncsFailed));
  row("Syncs", line);
  formatDuration(st.keyboardS, b, sizeof(b));
  snprintf(line, sizeof(line), "%s connected", b);
  row("Keyboard", line);

  // When you write: active time by hour over the last 30 days.
  y = static_cast<int16_t>(y + 10);
  target.text(Rect{static_cast<int16_t>(body.x + 4), y, body.width, lineH}, "When you write (last 30 days)", label);
  y = static_cast<int16_t>(y + lineH);
  const int16_t chartH = static_cast<int16_t>(body.bottom() - y - lineH - 8);
  if (chartH < 30) return;
  uint32_t peak = 1;
  for (uint32_t v : st.hourActiveS) peak = v > peak ? v : peak;
  const int16_t slot = static_cast<int16_t>((body.width - 8) / 24);
  const int16_t x0 = static_cast<int16_t>(body.x + 4 + (body.width - 8 - slot * 24) / 2);
  for (int h = 0; h < 24; ++h) {
    const int16_t barH = static_cast<int16_t>(static_cast<uint64_t>(st.hourActiveS[h]) * chartH / peak);
    const int16_t x = static_cast<int16_t>(x0 + h * slot);
    target.fill(Rect{x, static_cast<int16_t>(y + chartH - 1), static_cast<int16_t>(slot - 2), 1},
                Paint::solid(Color::Black));  // baseline
    if (barH > 0) {
      target.fill(Rect{x, static_cast<int16_t>(y + chartH - barH), static_cast<int16_t>(slot - 2), barH},
                  Paint::solid(Color::Black));
    }
    if (h % 6 == 0) {
      char hh[4];
      snprintf(hh, sizeof(hh), "%d", h);
      target.text(Rect{x, static_cast<int16_t>(y + chartH + 2), static_cast<int16_t>(slot * 3), lineH}, hh, label);
    }
  }
}
