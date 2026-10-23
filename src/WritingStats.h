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
// After an edit that inserted and/or removed characters at the caret.
void onEdit(const Document& doc, uint32_t inserted, uint32_t removed);
void setLocation(const journalmeta::Fix& fix);
// A journaling prompt was added to the entry this session (id from Prompts.h).
void addPrompt(const char* id);
// Before saving: writes/updates this session's record in doc's front matter.
// No-op if nothing was typed.
void writeInto(Document& doc);
void end();  // left the editor

}  // namespace WritingStats
