#pragma once

// Improv Wi-Fi over serial (https://www.improv-wifi.com/serial/): lets the web
// flasher (ESP Web Tools) send Wi-Fi credentials over USB right after flashing,
// then offer a "Visit Device" link to the setup page.
//
// A background task answers the protocol from the first moments after boot, so
// the flasher finds the device while the display is still initializing. The
// actual Wi-Fi join happens on the main loop (it has to pause BLE), which
// picks requests up with takeWifiRequest() and answers with finish().

#include <Arduino.h>

namespace Improv {

// Starts the serial listener task. `provisioned` = Wi-Fi credentials are
// already configured.
void begin(bool provisioned);

// Updates whether Wi-Fi is configured (once settings have loaded).
void setProvisioned(bool provisioned);

// True once when the flasher has sent credentials to try.
bool takeWifiRequest(char ssid[33], char password[65]);
// Reports the outcome of a request; on success `url` is the setup page that
// the flasher links to ("Visit Device").
void finish(bool ok, const char* url);
// The setup page URL to advertise while it's up ("" when it isn't).
void setUrl(const char* url);

// Feeds one received byte to the protocol parser (the listener task does this;
// exposed for host tests).
void feedByte(uint8_t b);

}  // namespace Improv
