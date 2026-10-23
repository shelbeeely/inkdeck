#pragma once

// Writing times for journal sessions: when each session and each paragraph
// ("part") was written, active typing time (pauses over a minute don't count),
// keystrokes, words, and the session's location. Kept in RAM while the entry
// is open and written into its front matter on every save (see JournalMeta.h
// for the format), so GitJournal keeps it and it syncs with the entry.

#include <Arduino.h>

#include "Document.h"
#include "JournalMeta.h"

namespace WritingStats {

// A journal session started on `doc` (today's entry, heading just added).
void begin(const Document& doc);
bool active();
// After a key that changed the text; `typed` is the inserted character (0 for
// Enter, Backspace and other edits).
void onEdit(const Document& doc, char typed);
void setLocation(const journalmeta::Fix& fix);
// Before saving: writes/updates this session's record in doc's front matter.
// No-op if nothing was typed.
void writeInto(Document& doc);
void end();  // left the editor

}  // namespace WritingStats
