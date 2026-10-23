#include "Improv.h"
#include <cstdio>
#include <cstring>
SerialStub Serial;
static bool handler(const char* lines) {
  if (strstr(lines, "bad_key")) return false;
  fprintf(stderr, "[device] stored: %s", lines);
  return true;
}
int main() {
  Improv::setSettingsHandler(handler);
  Improv::begin(false);
  // A boot log line left unterminated: our packets must still be recognized.
  const char* noise = "ESP-ROM:esp32c3 boot log without newline";
  Serial.write((const uint8_t*)noise, strlen(noise));
  unsigned char c;
  while (read(0, &c, 1) == 1) {
    Improv::feedByte(c);
    if (Improv::takeSettingsChanged()) fprintf(stderr, "[device] settings changed -> reload\n");
    char ssid[33], pw[65];
    if (Improv::takeWifiRequest(ssid, pw)) {
      fprintf(stderr, "[device] wifi request ssid=%s pw=%s\n", ssid, pw);
      if (strcmp(ssid, "Good Net") == 0) Improv::finish(true, "http://192.168.1.42/");
      else Improv::finish(false, nullptr);
    }
  }
}
