#pragma once

// User configuration: Wi-Fi credentials, the time zone, and the GitJournal
// repository used by Sync. Layers, all in the same "key = value" format, each
// overriding the one before:
//   1. flash-config.txt in the project, baked into the firmware at build time
//      (tools/flash_config.py);
//   2. settings saved on the device (NVS) by the web flasher's Wi-Fi step and
//      the Setup page (storeOnDevice);
//   3. /inkdeck.txt on the SD card. A .txt so it shows up in the browser and
//      can be edited on the device itself.

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

  // Resets to the built-in defaults, then applies each layer above. Returns
  // false if kPath doesn't exist.
  bool load();
  // Merges "key = value" lines into the settings stored on the device (an
  // empty value removes that key), then reloads. Returns false on NVS failure.
  bool storeOnDevice(const char* lines);
  // True if kPath sets `key` to a non-empty value (it then overrides the
  // device-stored one).
  bool sdOverrides(const char* key) const;
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
