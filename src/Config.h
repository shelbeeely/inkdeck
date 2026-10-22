#pragma once

// User configuration read from /inkdeck.txt on the SD card (key = value lines,
// '#' comments). A .txt so it shows up in the browser and can be edited on the
// device itself. Holds Wi-Fi credentials, the time zone, and the GitJournal
// (GitHub) repository used by Sync.

#include <Arduino.h>

struct Config {
  static constexpr const char* kPath = "/inkdeck.txt";

  char wifiSsid[33] = {0};
  char wifiPassword[65] = {0};
  char timezone[64] = "UTC0";  // POSIX TZ string
  char notesFolder[64] = "/Journal";  // SD folder mirrored with the repo root
  char journalSubfolder[64] = {0};    // where Today's entries go, inside notesFolder
  char githubRepo[100] = {0};         // "owner/name"
  char githubBranch[64] = "main";
  char githubToken[160] = {0};

  // Reads kPath. Returns false (leaving defaults) if it doesn't exist.
  bool load();
  // Writes a commented template to kPath if no file exists. Returns true if one
  // was created.
  bool writeTemplateIfMissing() const;

  bool hasWifi() const { return wifiSsid[0] != '\0'; }
  bool hasGitHub() const { return githubRepo[0] && githubToken[0] && strchr(githubRepo, '/'); }
};

extern Config g_config;
