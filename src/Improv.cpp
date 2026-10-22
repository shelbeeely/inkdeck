#include "Improv.h"

#include <FlashConfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace Improv {

namespace {

// Message types and states (Improv serial, protocol version 1).
constexpr uint8_t kTypeCurrentState = 0x01;
constexpr uint8_t kTypeErrorState = 0x02;
constexpr uint8_t kTypeRpc = 0x03;
constexpr uint8_t kTypeRpcResult = 0x04;

constexpr uint8_t kStateReady = 0x02;
constexpr uint8_t kStateProvisioning = 0x03;
constexpr uint8_t kStateProvisioned = 0x04;

constexpr uint8_t kErrNone = 0x00;
constexpr uint8_t kErrInvalidRpc = 0x01;
constexpr uint8_t kErrUnknownRpc = 0x02;
constexpr uint8_t kErrUnableToConnect = 0x03;

constexpr uint8_t kCmdWifiSettings = 0x01;
constexpr uint8_t kCmdCurrentState = 0x02;
constexpr uint8_t kCmdDeviceInfo = 0x03;
constexpr uint8_t kCmdInkDeckSettings = 0x80;  // InkDeck extension, see Improv.h

SemaphoreHandle_t g_lock = nullptr;
uint8_t g_state = kStateReady;
char g_url[64] = {0};
bool g_requestPending = false;
SettingsHandler g_settingsHandler = nullptr;
bool g_settingsChanged = false;
char g_reqSsid[33] = {0};
char g_reqPassword[65] = {0};

// Frames and writes one packet: "IMPROV" 0x01 <type> <len> <data> <checksum>.
// A leading newline ends any half-written log line, which the flasher would
// otherwise discard our packet along with.
void send(uint8_t type, const uint8_t* data, uint8_t len) {
  uint8_t pkt[1 + 9 + 255 + 2];
  size_t n = 0;
  pkt[n++] = '\n';
  const size_t start = n;
  memcpy(pkt + n, "IMPROV", 6);
  n += 6;
  pkt[n++] = 1;  // protocol version
  pkt[n++] = type;
  pkt[n++] = len;
  memcpy(pkt + n, data, len);
  n += len;
  uint8_t sum = 0;
  for (size_t i = start; i < n; ++i) sum = static_cast<uint8_t>(sum + pkt[i]);
  pkt[n++] = sum;
  pkt[n++] = '\n';
  Serial.write(pkt, n);
  Serial.flush();
}

void sendState(uint8_t state) { send(kTypeCurrentState, &state, 1); }
void sendError(uint8_t err) { send(kTypeErrorState, &err, 1); }

// RPC result: <command> <total len> then length-prefixed strings.
void sendResult(uint8_t command, const char* const* strings, uint8_t count) {
  uint8_t data[255];
  size_t n = 2;
  for (uint8_t i = 0; i < count; ++i) {
    const size_t len = strlen(strings[i]);
    if (n + 1 + len > sizeof(data)) break;
    data[n++] = static_cast<uint8_t>(len);
    memcpy(data + n, strings[i], len);
    n += len;
  }
  data[0] = command;
  data[1] = static_cast<uint8_t>(n - 2);
  send(kTypeRpcResult, data, static_cast<uint8_t>(n));
}

void sendUrlResult(uint8_t command) {
  // Called with g_lock held.
  if (g_url[0]) {
    const char* url = g_url;
    sendResult(command, &url, 1);
  } else {
    sendResult(command, nullptr, 0);
  }
}

void handleRpc(const uint8_t* data, uint8_t len) {
  if (len < 2 || data[1] != len - 2) {
    sendError(kErrInvalidRpc);
    return;
  }
  const uint8_t cmd = data[0];
  const uint8_t* body = data + 2;
  const uint8_t bodyLen = data[1];

  xSemaphoreTake(g_lock, portMAX_DELAY);
  switch (cmd) {
    case kCmdCurrentState:
      sendState(g_state);
      if (g_state == kStateProvisioned) sendUrlResult(kCmdCurrentState);
      break;
    case kCmdDeviceInfo: {
      const char* info[] = {"InkDeck", kInkDeckVersion, "ESP32-C3", "InkDeck"};
      sendResult(kCmdDeviceInfo, info, 4);
      break;
    }
    case kCmdWifiSettings: {
      // <ssid len> <ssid> <password len> <password>
      const uint8_t ssidLen = bodyLen > 0 ? body[0] : 0;
      const uint8_t passLen = bodyLen > 1 + ssidLen ? body[1 + ssidLen] : 0;
      if (ssidLen == 0 || ssidLen > 32 || passLen > 64 || 2u + ssidLen + passLen > bodyLen) {
        sendError(kErrInvalidRpc);
        break;
      }
      memcpy(g_reqSsid, body + 1, ssidLen);
      g_reqSsid[ssidLen] = '\0';
      memcpy(g_reqPassword, body + 2 + ssidLen, passLen);
      g_reqPassword[passLen] = '\0';
      g_requestPending = true;
      g_state = kStateProvisioning;
      sendError(kErrNone);
      sendState(g_state);
      break;
    }
    case kCmdInkDeckSettings: {
      char lines[256];
      memcpy(lines, body, bodyLen);
      lines[bodyLen] = '\0';
      if (bodyLen == 0 || strlen(lines) != bodyLen || !g_settingsHandler || !g_settingsHandler(lines)) {
        sendError(kErrInvalidRpc);
        break;
      }
      g_settingsChanged = true;
      const char* ok = "ok";
      sendError(kErrNone);
      sendResult(kCmdInkDeckSettings, &ok, 1);
      break;
    }
    default:
      sendError(kErrUnknownRpc);
      break;
  }
  xSemaphoreGive(g_lock);
}

}  // namespace

void feedByte(uint8_t b);

namespace {

uint8_t g_buf[9 + 255 + 1];
size_t g_have = 0;
size_t g_need = 0;

void task(void*) {
  for (;;) {
    const int c = Serial.read();
    if (c < 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    feedByte(static_cast<uint8_t>(c));
  }
}

}  // namespace

void feedByte(uint8_t b) {
  if (g_have < 6) {
    // Match the "IMPROV" header; restart on a mismatch (it may begin here).
    if (b == static_cast<uint8_t>("IMPROV"[g_have])) {
      g_buf[g_have++] = b;
    } else {
      g_have = (b == 'I') ? 1 : 0;
      if (g_have) g_buf[0] = b;
    }
    return;
  }
  g_buf[g_have++] = b;
  if (g_have == 9) g_need = 9 + g_buf[8] + 1;
  if (g_have < 9 || g_have < g_need) return;

  uint8_t sum = 0;
  for (size_t i = 0; i < g_need - 1; ++i) sum = static_cast<uint8_t>(sum + g_buf[i]);
  if (g_buf[6] == 1 && g_buf[7] == kTypeRpc && sum == g_buf[g_need - 1]) handleRpc(g_buf + 9, g_buf[8]);
  g_have = g_need = 0;
}

void begin(bool provisioned) {
  g_lock = xSemaphoreCreateMutex();
  g_state = provisioned ? kStateProvisioned : kStateReady;
  xTaskCreate(task, "improv", 6144, nullptr, 1, nullptr);  // settings writes go to NVS from here
}

void setProvisioned(bool provisioned) {
  if (!g_lock) return;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  if (g_state != kStateProvisioning) g_state = provisioned ? kStateProvisioned : kStateReady;
  xSemaphoreGive(g_lock);
}

bool takeWifiRequest(char ssid[33], char password[65]) {
  if (!g_lock) return false;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  const bool pending = g_requestPending;
  if (pending) {
    memcpy(ssid, g_reqSsid, sizeof(g_reqSsid));
    memcpy(password, g_reqPassword, sizeof(g_reqPassword));
    g_requestPending = false;
  }
  xSemaphoreGive(g_lock);
  return pending;
}

void finish(bool ok, const char* url) {
  if (!g_lock) return;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  if (ok) {
    strncpy(g_url, url ? url : "", sizeof(g_url) - 1);
    g_url[sizeof(g_url) - 1] = '\0';
    g_state = kStateProvisioned;
    sendState(g_state);
    sendUrlResult(kCmdWifiSettings);
  } else {
    g_state = kStateReady;
    sendError(kErrUnableToConnect);
    sendState(g_state);
  }
  xSemaphoreGive(g_lock);
}

void setSettingsHandler(SettingsHandler handler) { g_settingsHandler = handler; }  // before begin()

bool takeSettingsChanged() {
  if (!g_lock) return false;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  const bool changed = g_settingsChanged;
  g_settingsChanged = false;
  xSemaphoreGive(g_lock);
  return changed;
}

void setUrl(const char* url) {
  if (!g_lock) return;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  strncpy(g_url, url ? url : "", sizeof(g_url) - 1);
  g_url[sizeof(g_url) - 1] = '\0';
  xSemaphoreGive(g_lock);
}

}  // namespace Improv
