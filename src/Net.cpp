#include "Net.h"

#include <BleKeyboardHost.h>
#include <WiFi.h>

#include "screens.h"

namespace Net {

bool up(const char* ssid, const char* password, uint32_t timeoutMs) {
  BleHid.end(2000);  // frees the NimBLE heap for Wi-Fi + TLS
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("inkdeck");
  WiFi.begin(ssid, password);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(100);
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return false;
}

void down() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (!BleHid.begin("InkDeck")) {
    BleHid.end(2000);  // finish a slow teardown, then retry
    BleHid.begin("InkDeck");
  }
  refreshStatusLabel();
}

}  // namespace Net
