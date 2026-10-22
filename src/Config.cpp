#include "Config.h"

#include <SDCardManager.h>
#include <string.h>

Config g_config;

namespace {

void trim(char* s) {
  char* start = s;
  while (*start == ' ' || *start == '\t') ++start;
  if (start != s) memmove(s, start, strlen(start) + 1);
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
}

void copyField(char* dst, size_t dstsz, const char* value) {
  strncpy(dst, value, dstsz - 1);
  dst[dstsz - 1] = '\0';
}

// Folder settings: leading slash for SD paths, no trailing slash.
void normalizeFolder(char* s, bool leadingSlash) {
  size_t n = strlen(s);
  while (n > 1 && s[n - 1] == '/') s[--n] = '\0';
  if (leadingSlash) {
    if (s[0] != '/' && n + 1 < 64) {
      memmove(s + 1, s, n + 1);
      s[0] = '/';
    }
  } else {
    while (s[0] == '/') memmove(s, s + 1, strlen(s));
  }
}

const char kTemplate[] =
    "# InkDeck settings. Edit here or on a computer; changes apply when you\n"
    "# save this file, Sync, or restart. Lines are key = value; lines starting\n"
    "# with # are comments.\n"
    "\n"
    "# Wi-Fi used by Sync (2.4 GHz networks only).\n"
    "wifi_ssid =\n"
    "wifi_password =\n"
    "\n"
    "# Time zone as a POSIX TZ string. Examples:\n"
    "#   UTC0\n"
    "#   GMT0BST,M3.5.0/1,M10.5.0          (UK)\n"
    "#   CET-1CEST,M3.5.0,M10.5.0/3        (Central Europe)\n"
    "#   EST5EDT,M3.2.0,M11.1.0            (US Eastern)\n"
    "#   CST6CDT,M3.2.0,M11.1.0            (US Central)\n"
    "#   MST7MDT,M3.2.0,M11.1.0            (US Mountain)\n"
    "#   PST8PDT,M3.2.0,M11.1.0            (US Pacific)\n"
    "#   AEST-10AEDT,M10.1.0,M4.1.0/3      (Sydney)\n"
    "timezone = UTC0\n"
    "\n"
    "# SD card folder that mirrors your GitJournal repository, and the folder\n"
    "# inside it where Today's journal entries go (empty = top level).\n"
    "notes_folder = /Journal\n"
    "journal_subfolder =\n"
    "\n"
    "# GitJournal repository on GitHub. Create a fine-grained personal access\n"
    "# token limited to this repository with Contents: Read and write.\n"
    "github_repo = owner/journal\n"
    "github_branch = main\n"
    "github_token =\n";

}  // namespace

bool Config::load() {
  FsFile f = SdMan.open(kPath, O_RDONLY);
  if (!f) return false;
  char line[256];
  while (f.available()) {
    int n = f.fgets(line, sizeof(line));
    if (n <= 0) break;
    if (line[n - 1] == '\n') line[n - 1] = '\0';
    // '#' only starts a comment at the beginning of a line: passwords may contain it.
    const char* first = line + strspn(line, " \t");
    if (*first == '#') continue;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;
    trim(key);
    trim(value);
    if (strcmp(key, "wifi_ssid") == 0) copyField(wifiSsid, sizeof(wifiSsid), value);
    else if (strcmp(key, "wifi_password") == 0) copyField(wifiPassword, sizeof(wifiPassword), value);
    else if (strcmp(key, "timezone") == 0 && value[0]) copyField(timezone, sizeof(timezone), value);
    else if (strcmp(key, "notes_folder") == 0 && value[0]) copyField(notesFolder, sizeof(notesFolder), value);
    else if (strcmp(key, "journal_subfolder") == 0) copyField(journalSubfolder, sizeof(journalSubfolder), value);
    else if (strcmp(key, "github_repo") == 0) copyField(githubRepo, sizeof(githubRepo), value);
    else if (strcmp(key, "github_branch") == 0 && value[0]) copyField(githubBranch, sizeof(githubBranch), value);
    else if (strcmp(key, "github_token") == 0) copyField(githubToken, sizeof(githubToken), value);
  }
  f.close();
  normalizeFolder(notesFolder, true);
  normalizeFolder(journalSubfolder, false);
  if (strcmp(githubRepo, "owner/journal") == 0) githubRepo[0] = '\0';  // untouched template
  return true;
}

bool Config::writeTemplateIfMissing() const {
  if (SdMan.exists(kPath)) return false;
  FsFile f = SdMan.open(kPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  f.write(reinterpret_cast<const uint8_t*>(kTemplate), sizeof(kTemplate) - 1);
  f.close();
  return true;
}
