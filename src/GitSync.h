#pragma once

// Two-way sync between the SD notes folder and a GitJournal repository on
// GitHub, over the GitHub REST API (git trees + contents endpoints). Real git
// (packfiles, SSH) doesn't fit the ESP32-C3, but GitJournal only needs the
// files in the repo: every upload/delete here becomes a normal commit that the
// app pulls, and the app's commits are picked up on the next Sync.
//
// Only "*.md" files are synced. A small state file (<notes>/.inkdeck-sync)
// records each file's git blob id at the last sync, which tells local edits,
// remote edits and deletions apart. When both sides changed, nothing is lost:
// the device's version is kept as "<name> (InkDeck conflict).md" next to the
// remote one.

#include <Arduino.h>

namespace GitSync {

struct Result {
  bool ok = false;
  uint16_t pulled = 0;
  uint16_t pushed = 0;
  uint16_t deleted = 0;
  uint16_t conflicts = 0;
  uint16_t failed = 0;
  char error[64] = {0};
};

using Progress = void (*)(const char* line);

// Requires Wi-Fi to be connected and g_config.hasGitHub().
Result run(Progress progress);

}  // namespace GitSync
