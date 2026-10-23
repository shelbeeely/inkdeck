#include "Journal.h"

#include <SDCardManager.h>
#include <stdio.h>
#include <string.h>

#include "Clock.h"
#include "Config.h"
#include "PhoneLocation.h"
#include "SyncUtil.h"
#include "WritingStats.h"

namespace Journal {

namespace {
uint32_t textHash(const Document& doc) { return syncutil::pathHash(doc.text()) ^ doc.length(); }
}  // namespace

bool openToday(Document& doc, uint32_t& stubHash) {
  if (!Clock::valid()) return false;

  char dir[160];
  if (g_config.journalSubfolder[0]) {
    snprintf(dir, sizeof(dir), "%s/%s", g_config.notesFolder, g_config.journalSubfolder);
  } else {
    snprintf(dir, sizeof(dir), "%s", g_config.notesFolder);
  }
  if (!SdMan.exists(dir) && !SdMan.mkdir(dir)) return false;

  char date[16];
  Clock::formatDate(date, sizeof(date));
  char path[192];
  snprintf(path, sizeof(path), "%s/%s.md", dir, date);

  if (SdMan.exists(path)) {
    if (!doc.load(path)) return false;
  } else {
    char iso[32];
    Clock::formatIso(iso, sizeof(iso));
    char header[160];
    snprintf(header, sizeof(header), "---\ncreated: %s\nmodified: %s\ntype: Journal\n---\n", iso, iso);
    // Not saved yet: leaving without typing leaves no empty entry behind.
    doc.reset(path);
    doc.appendAtEnd(header);
  }

  char time[8];
  Clock::formatTime(time, sizeof(time));
  char heading[24];
  // Separate this session from earlier text with a blank line.
  const char* text = doc.text();
  const uint32_t n = doc.length();
  const bool endsBlank = n >= 2 && text[n - 1] == '\n' && text[n - 2] == '\n';
  const bool endsLine = n == 0 || text[n - 1] == '\n';
  const bool afterHeader = n >= 4 && strcmp(text + n - 4, "---\n") == 0;
  const char* sep = (endsBlank || afterHeader) ? "" : endsLine ? "\n" : "\n\n";
  snprintf(heading, sizeof(heading), "%s## %s\n\n", afterHeader ? "\n" : sep, time);
  doc.appendAtEnd(heading);
  stubHash = textHash(doc);
  // The session record also carries the location, so keep it for either.
  if (g_config.writingStats || g_config.locationFromPhone) WritingStats::begin(doc);
  PhoneLocation::request();
  return true;
}

bool isUntouched(const Document& doc, uint32_t stubHash) { return stubHash != 0 && textHash(doc) == stubHash; }

void touchModified(Document& doc) {
  if (!Clock::valid()) return;
  char iso[32];
  Clock::formatIso(iso, sizeof(iso));
  doc.setFrontMatterValue("modified", iso);
}

}  // namespace Journal
