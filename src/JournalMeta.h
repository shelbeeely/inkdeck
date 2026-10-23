#pragma once

// Pure helpers for journal metadata (no Arduino/SD dependencies, host-testable):
// the phone location format, word/paragraph counting, and the per-session
// writing record kept in the entry's YAML front matter:
//
//   sessions:
//     - start: 2026-10-23T14:32:05-07:00      # entry opened (the "## HH:MM" heading)
//       end: 2026-10-23T14:51:40-07:00        # last keystroke
//       active_s: 912                         # typing time; pauses over a minute don't count
//       keys: 2710
//       words: 433                            # words added this session
//       location: {lat: 45.523100, lon: -122.676500, accuracy_m: 12, time: ..., source: phone, place: "Portland"}
//       parts:                                # one per paragraph written, in order
//         - {start: "14:32:10", end: "14:36:02", active_s: 140, words: 61}

#include <stddef.h>
#include <stdint.h>

namespace journalmeta {

struct Fix {
  bool valid = false;
  double lat = 0;
  double lon = 0;
  uint32_t accuracyM = 0;
  int64_t time = 0;  // unix seconds of the fix
  char place[64] = {0};
  char source[8] = {0};  // "phone"
};

// Phone location characteristic value (UTF-8), see docs/phone-location.md:
//   "1;<lat>;<lon>;<accuracy m>;<unix time s>;<place, optional>"
bool parseLocation(const char* text, size_t len, Fix& out);

struct Part {
  int64_t start = 0;  // unix seconds of the first keystroke
  int64_t end = 0;    // unix seconds of the last keystroke
  uint32_t activeMs = 0;
  uint32_t words = 0;
};

struct Session {
  static constexpr uint8_t kMaxParts = 40;
  int64_t opened = 0;  // unix seconds
  int64_t lastKey = 0;
  uint32_t activeMs = 0;
  uint32_t keys = 0;
  int32_t words = 0;  // words added (can be negative after heavy editing)
  Fix location;
  Part parts[kMaxParts];
  uint8_t partCount = 0;
};

// Formats unix seconds as local "YYYY-MM-DDTHH:MM:SS+HH:MM" / "HH:MM:SS".
using FormatIso = void (*)(int64_t t, char* out, size_t outsz);
using FormatClock = void (*)(int64_t t, char* out, size_t outsz);

// Writes the session's list item (lines ending in '\n'), with the "-" at
// `indent` spaces. Returns the length, or 0 if it doesn't fit.
size_t buildSessionYaml(const Session& s, int indent, FormatIso iso, FormatClock clock, char* out, size_t outsz);

// Where the session item that starts "- start: <startIso>" lives in `doc`'s
// front matter, or where a new one goes. Replace [from, to) with the item
// (preceded by "sessions:\n" when needsKey) to update it.
struct Splice {
  uint32_t from = 0;
  uint32_t to = 0;
  bool needsKey = false;
  int indent = 2;  // match the list's existing indentation
};
bool findSessionBlock(const char* doc, uint32_t len, const char* startIso, Splice& out);

// Words (runs of non-space) in [from, to).
uint32_t countWords(const char* s, uint32_t from, uint32_t to);
// The paragraph (text between blank lines) containing `pos`.
void paragraphBounds(const char* s, uint32_t len, uint32_t pos, uint32_t& from, uint32_t& to);
// Offset where the body starts (after the front matter), 0 without one.
uint32_t bodyStart(const char* doc, uint32_t len);

}  // namespace journalmeta
