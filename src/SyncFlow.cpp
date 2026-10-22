#include "SyncFlow.h"

#include <BleKeyboardHost.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "AppState.h"
#include "Clock.h"
#include "Config.h"
#include "GitSync.h"
#include "screens.h"

namespace {

constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kNtpTimeoutMs = 10000;
constexpr uint32_t kMinPaintGapMs = 1500;  // e-ink: don't repaint for every file
uint32_t g_lastPaint = 0;

void addLine(const char* line) {
  AppState& st = g_state;
  if (st.syncLineCount == AppState::kSyncLines) {
    memmove(st.syncLines[0], st.syncLines[1], sizeof(st.syncLines[0]) * (AppState::kSyncLines - 1));
    --st.syncLineCount;
  }
  strncpy(st.syncLines[st.syncLineCount], line, sizeof(st.syncLines[0]) - 1);
  st.syncLines[st.syncLineCount][sizeof(st.syncLines[0]) - 1] = '\0';
  ++st.syncLineCount;
}

void paint(bool force) {
  if (!force && millis() - g_lastPaint < kMinPaintGapMs) return;
  g_lastPaint = millis();
  uiRenderNow();
}

void say(const char* line) {
  addLine(line);
  paint(true);
}

void progress(const char* line) {
  addLine(line);
  paint(false);
}

bool joinWifi() {
  char msg[64];
  snprintf(msg, sizeof(msg), "Joining %s...", g_config.wifiSsid);
  say(msg);
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_config.wifiSsid, g_config.wifiPassword);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) delay(100);
  if (WiFi.status() == WL_CONNECTED) return true;
  say("Wi-Fi failed. Check name/password");
  return false;
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void syncGit() {
  if (!g_config.hasRepo()) {
    say(g_config.repoUrl[0] ? "Can't read repo_url" : "No repo_url set up");
    return;
  }
  if (!g_config.repoIsGitHub()) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%.40s isn't supported yet", g_config.repoHost);
    say(msg);
    return;
  }
  if (!g_config.token[0]) {
    say("No git_token set up");
    return;
  }
  const GitSync::Result r = GitSync::run(progress);
  char msg[64];
  snprintf(msg, sizeof(msg), "%u down, %u up, %u deleted", r.pulled, r.pushed, r.deleted);
  addLine(msg);
  if (r.conflicts) {
    snprintf(msg, sizeof(msg), "%u conflict(s): kept both copies", r.conflicts);
    addLine(msg);
  }
  if (r.failed) {
    snprintf(msg, sizeof(msg), "%u file(s) failed; Sync again", r.failed);
    addLine(msg);
  }
  addLine(r.ok ? "Sync complete" : r.error[0] ? r.error : "Sync failed");
}

}  // namespace

void runSync(bool withGit) {
  AppState& st = g_state;
  st.syncLineCount = 0;
  st.syncDone = false;
  uiGotoScreen(AppScreen::Sync);

  // Pick up edits to /inkdeck.txt without a restart.
  g_config.load();
  Clock::applyTimezone(g_config.timezone);

  if (!g_config.hasWifi()) {
    const bool created = g_config.writeTemplateIfMissing();
    addLine("Wi-Fi isn't set up yet.");
    addLine(created ? "Created /inkdeck.txt on the SD card." : "Add it to /inkdeck.txt on the SD card,");
    addLine(created ? "Fill in Wi-Fi (and GitHub), then Sync." : "then Sync again.");
    st.syncDone = true;
    paint(true);
    return;
  }

  say("Pausing Bluetooth...");
  BleHid.end(2000);  // frees the NimBLE heap for Wi-Fi + TLS

  if (joinWifi()) {
    say("Setting the clock...");
    if (Clock::syncNtp(kNtpTimeoutMs)) {
      char date[16], time[8], msg[48];
      Clock::formatDate(date, sizeof(date));
      Clock::formatTime(time, sizeof(time));
      snprintf(msg, sizeof(msg), "Clock set: %s %s", date, time);
      say(msg);
    } else {
      say("Couldn't reach a time server");
    }
    if (withGit) syncGit();
  }
  wifiOff();

  if (!BleHid.begin("InkDeck")) {
    BleHid.end(2000);  // finish a slow teardown, then retry
    BleHid.begin("InkDeck");
  }
  refreshStatusLabel();
  st.syncDone = true;
  paint(true);
}
