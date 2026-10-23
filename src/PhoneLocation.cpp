#include "PhoneLocation.h"

#include <BleKeyboardHost.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include "Config.h"

namespace PhoneLocation {

namespace {

constexpr uint32_t kScanMs = 4000;
constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr int64_t kMaxFixAgeS = 30 * 60;  // older phone fixes aren't "where you were"

SemaphoreHandle_t g_lock = nullptr;
volatile bool g_busy = false;
bool g_ready = false;          // a fix is waiting for take()
journalmeta::Fix g_fix;        // the fix handed out by take()
journalmeta::Fix g_last;       // last good fix, reused within location_every
int64_t g_lastFixAt = 0;       // unix seconds when g_last was obtained

void publish(const journalmeta::Fix& fix) {
  xSemaphoreTake(g_lock, portMAX_DELAY);
  g_fix = fix;
  g_ready = fix.valid;
  if (fix.valid) {
    g_last = fix;
    g_lastFixAt = static_cast<int64_t>(time(nullptr));
  }
  xSemaphoreGive(g_lock);
}

// Scans with the keyboard host's scanner. Its callbacks stay in place (they
// just see the advertisers too); NimBLE keeps results only while we ask it to.
bool findPhone(NimBLEAddress& out) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setMaxResults(0xFF);
  NimBLEScanResults results = scan->getResults(kScanMs, false);
  const NimBLEUUID service(kServiceUuid);
  bool found = false;
  int bestRssi = -127;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(i);
    if (dev && dev->isConnectable() && dev->isAdvertisingService(service) && dev->getRSSI() > bestRssi) {
      out = dev->getAddress();
      bestRssi = dev->getRSSI();
      found = true;
    }
  }
  scan->setMaxResults(0);  // back to the keyboard host's callback-only mode
  scan->clearResults();
  return found;
}

journalmeta::Fix readFix(const NimBLEAddress& addr) {
  journalmeta::Fix fix;
  NimBLEClient* client = NimBLEDevice::createClient();
  if (!client) return fix;
  client->setConnectTimeout(kConnectTimeoutMs);
  if (client->connect(addr)) {
    NimBLERemoteService* svc = client->getService(kServiceUuid);
    NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(kLocationUuid) : nullptr;
    if (chr && chr->canRead()) {
      const NimBLEAttValue v = chr->readValue();
      journalmeta::parseLocation(reinterpret_cast<const char*>(v.data()), v.size(), fix);
    }
    client->disconnect();
  }
  NimBLEDevice::deleteClient(client);
  return fix;
}

void task(void*) {
  journalmeta::Fix fix;
  NimBLEAddress addr;
  if (findPhone(addr)) fix = readFix(addr);
  if (fix.valid && static_cast<int64_t>(time(nullptr)) - fix.time > kMaxFixAgeS) fix = journalmeta::Fix();
  publish(fix);
  g_busy = false;
  vTaskDelete(nullptr);
}

}  // namespace

void request() {
  if (!g_lock) g_lock = xSemaphoreCreateMutex();
  if (!g_config.locationFromPhone || g_busy) return;

  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (g_last.valid && now - g_lastFixAt < static_cast<int64_t>(g_config.locationEveryMin) * 60) {
    // Within location_every: this session is where the last one was.
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_fix = g_last;
    g_ready = g_last.valid;
    xSemaphoreGive(g_lock);
    return;
  }
  // Bluetooth must be up and not busy pairing (the scanner is shared).
  if (!BleHid.isRunning() || BleHid.isScanning()) return;
  g_busy = true;
  if (xTaskCreate(task, "phone-loc", 6144, nullptr, 1, nullptr) != pdPASS) g_busy = false;
}

bool take(journalmeta::Fix& out) {
  if (!g_lock) return false;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  const bool ready = g_ready;
  if (ready) out = g_fix;
  g_ready = false;
  xSemaphoreGive(g_lock);
  return ready;
}

void waitIdle(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (g_busy && millis() - start < timeoutMs) delay(20);
}

}  // namespace PhoneLocation
