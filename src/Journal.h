#pragma once

// Daily journal entries in GitJournal's format: one file per day named
// YYYY-MM-DD.md (GitJournal's "DateOnly" journal file name format) with YAML
// front matter (created / modified / type: Journal), so entries written on
// the device show up as journal notes in the GitJournal app after Sync.

#include <Arduino.h>

#include "Document.h"

namespace Journal {

// Opens (creating if needed) today's entry in `doc` and appends a "## HH:MM"
// heading for this session with the caret after it. Requires a valid clock.
// `stubHash` receives a hash of the opened text so leaving without typing can
// drop the heading silently (see isUntouched).
bool openToday(Document& doc, uint32_t& stubHash);
bool isUntouched(const Document& doc, uint32_t stubHash);

// Refreshes the front matter `modified:` value before a save (no-op without
// front matter or a valid clock).
void touchModified(Document& doc);

}  // namespace Journal
