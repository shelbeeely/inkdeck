#include "SetupFlow.h"

#include <WiFi.h>
#include <qrcode.h>
#include <stdio.h>
#include <string.h>

#include "AppState.h"
#include "Clock.h"
#include "Config.h"
#include "Improv.h"
#include "Net.h"
#include "SetupServer.h"
#include "SyncFlow.h"
#include "screens.h"

namespace {

constexpr uint32_t kSetupTimeoutMs = 15 * 60 * 1000;  // don't leave the page up forever
constexpr int kQrMax = 41;                            // QR version 6, plenty for a URL

bool g_wifiUp = false;
uint32_t g_lastActivity = 0;
uint8_t g_qr[kQrMax * kQrMax];
int g_qrSize = 0;

void setStatus(const char* a, const char* b = "") {
  snprintf(g_state.setupStatus, sizeof(g_state.setupStatus), "%s", a);
  snprintf(g_state.setupDetail, sizeof(g_state.setupDetail), "%s", b);
}

void copyQr(esp_qrcode_handle_t qr) {
  const int size = esp_qrcode_get_size(qr);
  if (size > kQrMax) return;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) g_qr[y * size + x] = esp_qrcode_get_module(qr, x, y) ? 1 : 0;
  }
  g_qrSize = size;
}

void makeQr(const char* text) {
  g_qrSize = 0;
  esp_qrcode_config_t cfg = {copyQr, 6, ESP_QRCODE_ECC_LOW};
  esp_qrcode_generate(&cfg, text);
}

// Saves Wi-Fi credentials from the flasher alongside the other settings.
void storeWifi(const char* ssid, const char* password) {
  String lines = "wifi_ssid = ";
  lines += ssid;
  lines += "\nwifi_password = ";
  lines += password;
  lines += '\n';
  g_config.storeOnDevice(lines.c_str());
}

}  // namespace

void startSetup(const char* ssid, const char* password, bool fromImprov) {
  AppState& st = g_state;
  if (st.screen == AppScreen::Setup) endSetup(false);  // e.g. new credentials from the flasher
  if (st.screen == AppScreen::Editor && documentHasUserChanges()) saveDocument();

  g_qrSize = 0;
  st.setupSaved = false;
  st.setupUrl[0] = '\0';
  char msg[64];
  snprintf(msg, sizeof(msg), "Joining %s...", ssid);
  setStatus(msg, "Bluetooth is paused during setup.");
  uiGotoScreen(AppScreen::Setup);
  uiRenderNow();

  g_wifiUp = Net::up(ssid, password);
  if (!g_wifiUp) {
    if (fromImprov) Improv::finish(false, nullptr);
    Net::down();
    snprintf(msg, sizeof(msg), "Couldn't join %s", ssid);
    setStatus(msg, "Check the network name and password, then try again.");
    uiInvalidate(false);
    return;
  }

  if (fromImprov) storeWifi(ssid, password);
  Clock::applyTimezone(g_config.timezone);
  Clock::syncNtp(5000);

  snprintf(st.setupUrl, sizeof(st.setupUrl), "http://%s/", WiFi.localIP().toString().c_str());
  makeQr(st.setupUrl);
  SetupServer::start();
  if (fromImprov) Improv::finish(true, st.setupUrl);
  else Improv::setUrl(st.setupUrl);

  snprintf(msg, sizeof(msg), "On a phone or computer on %s, open:", ssid);
  setStatus(msg, "Then enter your GitJournal repository there.");
  g_lastActivity = millis();
  uiInvalidate(true);
}

void setupLoop() {
  if (!g_wifiUp) return;
  SetupServer::poll();
  if (SetupServer::takeSaved()) {
    g_state.setupSaved = true;
    g_lastActivity = millis();
    setStatus("Settings saved.", "OK: sync now.  Back: finish.");
    uiInvalidate(false);
  }
  if (millis() - g_lastActivity > kSetupTimeoutMs) endSetup(false);
}

void endSetup(bool thenSync) {
  if (g_wifiUp) {
    SetupServer::stop();
    Improv::setUrl("");
    Net::down();
    g_wifiUp = false;
  }
  g_qrSize = 0;
  g_state.setupUrl[0] = '\0';
  loadDir();
  uiGotoScreen(AppScreen::Browser);
  if (thenSync) runSync(true);
}

int setupQrSize() { return g_qrSize; }
bool setupQrModule(int x, int y) { return g_qr[y * g_qrSize + x] != 0; }
