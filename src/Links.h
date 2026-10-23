#pragma once

// Keeps links working when a note is renamed: every note under the same root
// (the notes folder, or the whole card for notes outside it) gets its links
// to the old name rewritten (see LinkRewrite.h for the rules).

#include <Arduino.h>

namespace Links {

struct Result {
  uint16_t links = 0;         // links rewritten
  uint16_t notes = 0;         // notes changed
  uint16_t skipped = 0;       // notes too large to update, or failed to save
  bool nameShared = false;    // another note has the old name: bare [[name]] links were left alone
};

// Call after renaming `oldFull` to `newFull` (absolute SD paths, same folder).
Result afterRename(const char* oldFull, const char* newFull);

}  // namespace Links
