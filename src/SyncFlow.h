#pragma once

// The Sync screen's work: pause Bluetooth, join Wi-Fi, set the clock over NTP,
// optionally sync the GitJournal repository, then turn Wi-Fi off and bring
// Bluetooth back. Blocking; progress is painted as it goes.

void runSync(bool withGit);
