#include "UsageLog.h"

#include <BatteryMonitor.h>
#include <SDCardManager.h>
#include <esp_sleep.h>
#include <string.h>
#include <time.h>

#include "Clock.h"
#include "Config.h"
#include "JournalMeta.h"

namespace UsageLog {

namespace {

bool g_editing = false;
bool g_journal = false;
journalmeta::Counters g_counters;
int32_t g_wordsAtStart = 0;
char g_editPath[160] = {0};
uint32_t g_keyboardSince = 0;

void usageDir(char* out, size_t outsz) { snprintf(out, outsz, "%s/_inkdeck/usage", g_config.notesFolder); }

// Path as logged: relative to the notes folder when inside it.
void logPath(const char* full, char* out, size_t outsz) {
  const size_t nf = strlen(g_config.notesFolder);
  if (strncmp(full, g_config.notesFolder, nf) == 0 && full[nf] == '/') full += nf + 1;
  snprintf(out, outsz, "%s", full);
}

int32_t docWords(const Document& doc) {
  const char* t = doc.text();
  const uint32_t n = doc.length();
  return static_cast<int32_t>(journalmeta::countWords(t, journalmeta::bodyStart(t, n), n));
}

void append(usage::Event& e) {
  if (!Clock::valid() || !SdMan.ready()) return;
  const time_t now = time(nullptr);
  Clock::formatIsoAt(static_cast<int64_t>(now), e.time, sizeof(e.time));
  if (e.battery < 0) e.battery = batteryPercent();

  char dir[96];
  usageDir(dir, sizeof(dir));
  if (!SdMan.exists(dir)) SdMan.mkdir(dir);
  char path[128];
  snprintf(path, sizeof(path), "%s/%.7s.csv", dir, e.time);  // YYYY-MM
  FsFile f = SdMan.open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return;
  if (f.size() == 0) {
    f.write(reinterpret_cast<const uint8_t*>(usage::kHeader), strlen(usage::kHeader));
    f.write('\n');
  }
  char line[512];
  const size_t n = usage::format(e, line, sizeof(line));
  if (n) {
    f.write(reinterpret_cast<const uint8_t*>(line), n);
    f.write('\n');
  }
  f.close();
}

}  // namespace

int batteryPercent() {
  static BatteryMonitor battery;
  uint16_t pct = 0;
  return battery.readPercentageChecked(pct) && pct <= 100 ? static_cast<int>(pct) : -1;
}

void boot() {
  usage::Event e;
  strcpy(e.event, "boot");
  strcpy(e.detail, esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED ? "cold" : "wake");
  append(e);
}

void sleep() {
  usage::Event e;
  strcpy(e.event, "sleep");
  append(e);
}

void editBegin(const Document& doc, bool journal) {
  g_counters = journalmeta::Counters();
  g_counters.opened = static_cast<int64_t>(time(nullptr));
  g_wordsAtStart = docWords(doc);
  g_journal = journal;
  logPath(doc.path(), g_editPath, sizeof(g_editPath));
  g_editing = true;
}

void edit(const Document& doc, uint32_t inserted, uint32_t removed) {
  if (!g_editing) return;
  uint32_t from, to;
  journalmeta::paragraphBounds(doc.text(), doc.length(), doc.cursor(), from, to);
  uint32_t rest = to;
  while (rest < doc.length() && (doc.text()[rest] == '\n' || doc.text()[rest] == ' ')) ++rest;
  g_counters.edit(millis(), static_cast<int64_t>(time(nullptr)), inserted, removed, rest < doc.length());
}

void editEnd(const Document& doc) {
  if (!g_editing) return;
  g_editing = false;
  if (g_counters.keys == 0) return;
  usage::Event e;
  strcpy(e.event, "session");
  snprintf(e.path, sizeof(e.path), "%s", g_editPath);
  e.durationS = static_cast<uint32_t>(static_cast<int64_t>(time(nullptr)) - g_counters.opened);
  e.activeS = g_counters.activeMs / 1000;
  e.keys = g_counters.keys;
  e.typed = g_counters.typed;
  e.deleted = g_counters.deleted;
  e.words = docWords(doc) - g_wordsAtStart;
  e.wpm = journalmeta::wordsPerMinute(e.words, g_counters.activeMs);
  strcpy(e.detail, g_journal ? "journal" : "note");
  append(e);
}

void fileEvent(const char* event, const char* fullPath, const char* detail) {
  usage::Event e;
  snprintf(e.event, sizeof(e.event), "%s", event);
  logPath(fullPath, e.path, sizeof(e.path));
  if (detail && detail[0]) {
    char rel[160];
    logPath(detail, rel, sizeof(rel));
    snprintf(e.detail, sizeof(e.detail), "%s", rel);
  }
  append(e);
}

void sync(uint32_t durationS, const char* detail) {
  usage::Event e;
  strcpy(e.event, "sync");
  e.durationS = durationS;
  snprintf(e.detail, sizeof(e.detail), "%s", detail);
  append(e);
}

void keyboard(bool connected, const char* name) {
  usage::Event e;
  strcpy(e.event, "keyboard");
  if (connected) {
    g_keyboardSince = millis();
    snprintf(e.detail, sizeof(e.detail), "connected %s", name ? name : "");
  } else {
    e.durationS = g_keyboardSince ? (millis() - g_keyboardSince) / 1000 : 0;
    g_keyboardSince = 0;
    strcpy(e.detail, "disconnected");
  }
  append(e);
}

bool load(usage::Stats& stats, int32_t& today) {
  if (!Clock::valid()) return false;
  const time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  today = usage::daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  stats = usage::Stats();
  stats.today = today;

  // Month files sort by name; read them oldest first so recent days survive
  // if the day table fills up.
  char dir[96];
  usageDir(dir, sizeof(dir));
  constexpr int kMaxFiles = 48;
  char names[kMaxFiles][16];
  int count = 0;
  FsFile d = SdMan.open(dir, O_RDONLY);
  if (d && d.isDirectory()) {
    char name[32];
    for (FsFile f = d.openNextFile(); f && count < kMaxFiles; f = d.openNextFile()) {
      f.getName(name, sizeof(name));
      f.close();
      if (strlen(name) == 11 && strcmp(name + 7, ".csv") == 0) strcpy(names[count++], name);
    }
    d.close();
  }
  for (int i = 1; i < count; ++i) {
    char tmp[16];
    strcpy(tmp, names[i]);
    int j = i - 1;
    while (j >= 0 && strcmp(names[j], tmp) > 0) {
      strcpy(names[j + 1], names[j]);
      --j;
    }
    strcpy(names[j + 1], tmp);
  }
  char line[512];
  for (int i = 0; i < count; ++i) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
    FsFile f = SdMan.open(path, O_RDONLY);
    while (f && f.available()) {
      if (f.fgets(line, sizeof(line)) <= 0) break;
      usage::Event e;
      if (usage::parse(line, e)) stats.add(e);
    }
    if (f) f.close();
  }
  return true;
}

}  // namespace UsageLog
