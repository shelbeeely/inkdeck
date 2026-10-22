#pragma once

// The Setup screen: join Wi-Fi (credentials from the web flasher via Improv,
// or the configured ones from Menu > Setup over Wi-Fi), serve the setup page
// on the local network, and show its address (with a QR code) until Back.

#include <Arduino.h>

// fromImprov: answer the web flasher, and save the credentials on success.
void startSetup(const char* ssid, const char* password, bool fromImprov);
void setupLoop();               // main loop, while the Setup screen is up
void endSetup(bool thenSync);   // close the page, Wi-Fi off, Bluetooth back

// QR code of the setup address for the screen (size 0 when there is none).
int setupQrSize();
bool setupQrModule(int x, int y);
