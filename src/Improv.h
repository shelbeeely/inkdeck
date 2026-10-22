#pragma once

// Improv Wi-Fi over serial (https://www.improv-wifi.com/serial/): lets the web
// flasher (ESP Web Tools) send Wi-Fi credentials over USB right after flashing,
// then offer a "Visit Device" link to the setup page.
//
// InkDeck extension, same framing: RPC command 0x80 carries settings as
// "key = value" lines (UTF-8, one or more per packet, up to 253 bytes), so
// the flasher page can also send the repository, token and time zone. The
// result is the single string "ok"; a rejected packet gets an
// INVALID_RPC_PACKET error.
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

// Called from the listener task with the lines of a settings packet (0x80).
// Returns false to reject them. Must be safe off the main loop.
using SettingsHandler = bool (*)(const char* lines);
void setSettingsHandler(SettingsHandler handler);  // call before begin()
// True once after a settings packet was accepted (reload config then).
bool takeSettingsChanged();

// Feeds one received byte to the protocol parser (the listener task does this;
// exposed for host tests).
void feedByte(uint8_t b);

}  // namespace Improv
