#pragma once

// User configuration: Wi-Fi credentials, the time zone, and the GitJournal
// repository used by Sync. Two layers, same "key = value" format:
//   1. flash-config.txt in the project, baked into the firmware at build time
//      (tools/flash_config.py), so a freshly flashed device is ready to Sync;
//   2. /inkdeck.txt on the SD card, whose keys override the baked ones. A .txt
//      so it shows up in the browser and can be edited on the device itself.

#include <Arduino.h>

struct Config {
  static constexpr const char* kPath = "/inkdeck.txt";

  char wifiSsid[33] = {0};
  char wifiPassword[65] = {0};
  char timezone[64] = "UTC0";  // POSIX TZ string
  char notesFolder[64] = "/Journal";  // SD folder mirrored with the repo root
  char journalSubfolder[64] = {0};    // where Today's entries go, inside notesFolder
  char repoUrl[160] = {0};            // as written: any URL form, see parseRepoUrl
  char repoHost[64] = {0};            // parsed from repoUrl, e.g. "github.com"
  char repo[100] = {0};               // parsed from repoUrl: "owner/name"
  char branch[64] = "main";
  char token[160] = {0};

  // Resets to the built-in defaults plus the values baked in at flash time,
  // then applies kPath on top. Returns false if kPath doesn't exist.
  bool load();
  // Writes a commented template to kPath if no file exists. Returns true if one
  // was created.
  bool writeTemplateIfMissing() const;

  void applyLine(char* line);  // one "key = value" line

  bool hasWifi() const { return wifiSsid[0] != '\0'; }
  bool hasRepo() const { return repo[0] != '\0'; }
  bool repoIsGitHub() const { return strcmp(repoHost, "github.com") == 0; }
  bool hasGitHub() const { return hasRepo() && repoIsGitHub() && token[0]; }
};

extern Config g_config;
