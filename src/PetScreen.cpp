#include <SDCardManager.h>
#include <stdio.h>
#include <string.h>

#include "Companion.h"
#include "Config.h"
#include "UsageLog.h"
#include "screens.h"

// The Pet screen: the journaling pet, level and streaks from the server's
// companion.md (Companion.h). Fullness also counts what was written today
// since the last sync, so feeding it shows straight away.

using namespace freeink::ui;

namespace {
companion::Info* g_info = nullptr;  // only while the screen is up
int32_t g_todayWords = 0;
}  // namespace

bool petLoad() {
  if (!g_info) g_info = new companion::Info();
  if (!g_info) return false;
  char path[128];
  snprintf(path, sizeof(path), "%s%s", g_config.notesFolder, companion::kPath);
  constexpr size_t kMax = 2048;  // the front matter is ~1 KB; the body isn't needed
  char* buf = new char[kMax];
  size_t n = 0;
  FsFile f = SdMan.open(path, O_RDONLY);
  if (buf && f) {
    const int r = f.read(reinterpret_cast<uint8_t*>(buf), kMax);
    n = r > 0 ? static_cast<size_t>(r) : 0;
  }
  if (f) f.close();
  if (buf) companion::parse(buf, n, *g_info);
  delete[] buf;

  // Today's words, from the usage log (the same source the server counts).
  g_todayWords = 0;
  auto* st = new usage::Stats();
  int32_t today = 0;
  if (st && UsageLog::load(*st, today)) g_todayWords = usage::summarize(*st, today).todayWords;
  delete st;
  return true;
}

void petFree() {
  delete g_info;
  g_info = nullptr;
}

void petScreen(ScreenT& s, void*) {
  const companion::Info* c = g_info;
  s.header(c && c->valid && c->petName[0] ? c->petName : "Pet", nullptr, "");
  uiDrawHardwareFooter("Back", "", "", "");
  if (!c) return;

  Rect body = s.body();
  auto& target = s.frame().target();
  TextStyle label = g_theme.smallText;
  label.maxLines = 1;
  TextStyle value = g_theme.bodyText;
  value.maxLines = 1;
  const int16_t lineH = static_cast<int16_t>(target.lineHeight(value.font) + 10);
  const Paint ink = Paint::solid(Color::Black);

  if (!c->valid) {
    TextStyle note = g_theme.bodyText;
    note.maxLines = 8;
    target.text(Rect{static_cast<int16_t>(body.x + 8), static_cast<int16_t>(body.y + 24),
                     static_cast<int16_t>(body.width - 16), static_cast<int16_t>(lineH * 8)},
                "No pet yet. It lives on the InkDeck server: run it with WRITE_BACK=1 "
                "(see server/README.md), then Sync.",
                note);
    return;
  }

  // The pet, bigger as it grows up.
  const char* mood = companion::liveMood(*c, g_todayWords);
  uint16_t rows[16];
  companion::sprite(*c, mood, rows);
  int16_t scale = 8;
  if (strcmp(c->petStage, "baby") == 0) scale = 7;
  else if (strcmp(c->petStage, "teen") == 0) scale = 9;
  else if (strcmp(c->petStage, "adult") == 0) scale = 10;
  const int16_t box = 16 * 10;
  const int16_t px = static_cast<int16_t>(body.x + (body.width - 16 * scale) / 2);
  const int16_t py = static_cast<int16_t>(body.y + 10 + (box - 16 * scale));
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      if (rows[y] & (0x8000 >> x)) {
        target.fill(Rect{static_cast<int16_t>(px + x * scale), static_cast<int16_t>(py + y * scale), scale, scale}, ink);
      }
    }
  }
  int16_t y = static_cast<int16_t>(body.y + 10 + box + 8);

  char line[128];
  TextStyle center = value;
  center.align = TextAlign::Center;
  TextStyle centerSmall = label;
  centerSmall.align = TextAlign::Center;
  centerSmall.maxLines = 2;
  if (strcmp(c->petState, "egg") == 0) {
    snprintf(line, sizeof(line), "An egg - %s writing days to hatch", c->hatchProgress);
  } else if (strcmp(c->petState, "asleep") == 0) {
    snprintf(line, sizeof(line), "Hibernating - write on 3 days to wake it");
  } else if (strcmp(c->petState, "gone") == 0) {
    snprintf(line, sizeof(line), "A new egg comes with your next entry");
  } else {
    snprintf(line, sizeof(line), "%s %s%s%s, %ld days old", mood, c->petStage, c->petForm[0] ? " " : "", c->petForm,
             static_cast<long>(c->petAgeDays));
  }
  target.text(Rect{body.x, y, body.width, lineH}, line, center);
  y = static_cast<int16_t>(y + lineH);
  if (c->petSays[0]) {
    snprintf(line, sizeof(line), "\"%s\"", c->petSays);
    target.text(Rect{static_cast<int16_t>(body.x + 8), y, static_cast<int16_t>(body.width - 16),
                     static_cast<int16_t>(lineH * 2)},
                line, centerSmall);
    y = static_cast<int16_t>(y + lineH + lineH / 2);
  }

  const int16_t labelW = 128;
  const int16_t barW = static_cast<int16_t>(body.width - labelW - 60);
  auto meter = [&](const char* name, int32_t v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    target.text(Rect{static_cast<int16_t>(body.x + 4), static_cast<int16_t>(y + 3), labelW, lineH}, name, label);
    const Rect bar{static_cast<int16_t>(body.x + labelW), static_cast<int16_t>(y + lineH / 2 - 8), barW, 16};
    target.stroke(bar, ink, 2, 4);
    const int16_t w = static_cast<int16_t>((barW - 6) * v / 100);
    if (w > 0) target.fill(Rect{static_cast<int16_t>(bar.x + 3), static_cast<int16_t>(bar.y + 3), w, 10}, ink, 2);
    char n[8];
    snprintf(n, sizeof(n), "%ld", static_cast<long>(v));
    target.text(Rect{static_cast<int16_t>(bar.x + barW + 8), static_cast<int16_t>(y + 3), 52, lineH}, n, label);
    y = static_cast<int16_t>(y + lineH);
  };
  auto row = [&](const char* name, const char* text) {
    target.text(Rect{static_cast<int16_t>(body.x + 4), static_cast<int16_t>(y + 3), labelW, lineH}, name, label);
    target.text(Rect{static_cast<int16_t>(body.x + labelW), y, static_cast<int16_t>(body.width - labelW), lineH}, text,
                value);
    y = static_cast<int16_t>(y + lineH);
  };

  if (strcmp(c->petState, "alive") == 0) {
    meter("Fullness", companion::liveFullness(*c, g_todayWords));
    meter("Happiness", c->happiness);
    meter("Health", c->health);
  }
  y = static_cast<int16_t>(y + 8);

  const int32_t span = c->xpNext > c->xpLevelStart ? c->xpNext - c->xpLevelStart : 1;
  snprintf(line, sizeof(line), "Level %ld", static_cast<long>(c->level));
  meter(line, (c->xp - c->xpLevelStart) * 100 / span);
  snprintf(line, sizeof(line), "%ld day%s (best %ld), %ld rest left", static_cast<long>(c->streak),
           c->streak == 1 ? "" : "s", static_cast<long>(c->bestStreak), static_cast<long>(c->restDaysLeft));
  row("Streak", line);
  snprintf(line, sizeof(line), "%ld / %ld words%s", static_cast<long>(g_todayWords), static_cast<long>(c->goal),
           g_todayWords >= c->goal ? " - done!" : "");
  row("Today", line);
  snprintf(line, sizeof(line), "%ld of %ld%s%s", static_cast<long>(c->achievements),
           static_cast<long>(c->achievementsTotal), c->latestAchievement[0] ? ", latest: " : "", c->latestAchievement);
  row("Badges", line);
  if (c->insight[0] && y + lineH * 2 <= body.bottom()) {
    // Why bother: what your own journal shows (from the server, prompts.py).
    TextStyle note = label;
    note.maxLines = 2;
    target.text(Rect{static_cast<int16_t>(body.x + 4), static_cast<int16_t>(y + 4), static_cast<int16_t>(body.width - 8),
                     static_cast<int16_t>(lineH * 2 - 8)},
                c->insight, note);
    y = static_cast<int16_t>(y + lineH * 2);
  }
  if (c->updated[0] && y + lineH <= body.bottom()) {
    snprintf(line, sizeof(line), "%.16s", c->updated);
    for (char* p = line; *p; ++p) {
      if (*p == 'T') *p = ' ';
    }
    row("Updated", line);
  }
}
