#pragma once

// The setup web page, served on the local network while the Setup screen is
// up (after the web flasher's Wi-Fi step, or Menu > Setup over Wi-Fi). Takes the
// repository URL, token, branch, time zone and folders, and saves them on the
// device (Config::storeOnDevice). Nothing is reachable once Setup is closed.

#include <Arduino.h>

namespace SetupServer {

void start();
void stop();
void poll();          // call from the main loop while running
bool takeSaved();     // true once after the page saved settings

}  // namespace SetupServer
