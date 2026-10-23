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
//       wpm: 28                               # words per active minute
//       typed: 2390                           # characters typed (or pasted)
//       deleted: 270                          # characters deleted
//       pauses: 3                             # breaks of over a minute
//       longest_pause_s: 410
//       longest_burst_s: 140                  # longest stretch without a 5 s gap
//       first_key_s: 6                        # from opening to the first keystroke
//       revisions: 12                         # edits to earlier paragraphs
//       prompts: [grat-three]                 # journaling prompts chosen (Prompts.h)
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

// Typing activity of one editing session (journal or any file).
struct Counters {
  static constexpr uint32_t kIdleGapMs = 60000;  // a longer gap is a pause, not typing
  static constexpr uint32_t kBurstGapMs = 5000;  // a burst ends at a gap this long
  int64_t opened = 0;    // unix seconds
  int64_t firstKey = 0;  // unix seconds (0 = nothing typed)
  int64_t lastKey = 0;
  uint32_t lastKeyMs = 0;  // millis() of the last edit
  uint32_t activeMs = 0;
  uint32_t keys = 0;
  uint32_t typed = 0;
  uint32_t deleted = 0;
  uint32_t pauses = 0;
  uint32_t longestPauseMs = 0;
  uint32_t burstMs = 0;
  uint32_t longestBurstMs = 0;
  uint32_t revisions = 0;
  // Returns the time credited as typing (for per-part totals).
  uint32_t edit(uint32_t ms, int64_t now, uint32_t inserted, uint32_t removed, bool revision);
};

// Words per active minute, or 0 with too little typing to say.
uint32_t wordsPerMinute(int32_t words, uint32_t activeMs);

struct Session {
  static constexpr uint8_t kMaxParts = 40;
  int64_t opened = 0;  // unix seconds
  int64_t lastKey = 0;
  uint32_t activeMs = 0;
  uint32_t keys = 0;
  int32_t words = 0;  // words added (can be negative after heavy editing)
  Counters c;
  Fix location;
  char placeTag[32] = {0};   // named place the session was at ("Desk")
  char ambient[200] = {0};   // flow map from the phone ("{lux: 320, noise: \"chatter\"}")
  char prompts[96] = {0};    // journaling prompt ids chosen this session, comma-separated
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
// The same for any front matter list ("checkins"), keyed by `headKey`; a
// value that matches nothing gives the list's end (to append).
bool findListItem(const char* doc, uint32_t len, const char* listKey, const char* headKey, const char* headValue,
                  Splice& out);
// A top-level "key:" block (its line plus indented lines after it), or
// where a new one goes (before the closing ---).
bool findTopBlock(const char* doc, uint32_t len, const char* key, Splice& out);
// A "key:" line directly inside the list item [itemFrom, itemTo) whose "-" is
// at `indent`, or where a new one goes (before "parts:", else at the end).
void findItemLine(const char* doc, uint32_t itemFrom, uint32_t itemTo, int indent, const char* key, Splice& out);

// Calls fn(startIso, itemFrom, itemTo, indent) for each "sessions:" item.
template <typename Fn>
void forEachSession(const char* doc, uint32_t len, Fn fn);

// Value text of `key:` on a line inside [from, to) (copied, trimmed); false if absent.
bool itemValue(const char* doc, uint32_t from, uint32_t to, int indent, const char* key, char* out, size_t outsz);
// "lat: 45.5, lon: -122.6" inside a flow map.
bool flowLatLon(const char* flow, double& lat, double& lon);

// Phone ambient characteristic ("1;lux=320;noise_db=48;noise=chatter") as a
// YAML flow map; numbers stay bare, other values are quoted. False if empty.
bool parseAmbient(const char* text, size_t len, char* out, size_t outsz);

// Words (runs of non-space) in [from, to).
uint32_t countWords(const char* s, uint32_t from, uint32_t to);
// The paragraph (text between blank lines) containing `pos`.
void paragraphBounds(const char* s, uint32_t len, uint32_t pos, uint32_t& from, uint32_t& to);
// Offset where the body starts (after the front matter), 0 without one.
uint32_t bodyStart(const char* doc, uint32_t len);

// Line end helper for the template below.
uint32_t lineEndAt(const char* s, uint32_t len, uint32_t p);

template <typename Fn>
void forEachSession(const char* doc, uint32_t len, Fn fn) {
  Splice list;
  if (!findListItem(doc, len, "sessions", "start", "\x01", list) || list.needsKey) return;
  // Walk back from the list end to the "sessions:" line, then forward item by item.
  uint32_t key = 0;
  for (uint32_t p = 4; p < list.from; p = lineEndAt(doc, len, p) + 1) {
    if (len - p >= 9 && doc[p] == 's' && __builtin_memcmp(doc + p, "sessions:", 9) == 0) key = p;
  }
  if (!key) return;
  uint32_t p = lineEndAt(doc, len, key) + 1;
  while (p < list.from) {
    const uint32_t e = lineEndAt(doc, len, p);
    int i = 0;
    while (doc[p + i] == ' ') ++i;
    if (i == list.indent && doc[p + i] == '-' && len - (p + i) > 9 && __builtin_memcmp(doc + p + i, "- start: ", 9) == 0) {
      char iso[40];
      uint32_t n = e - (p + i + 9);
      if (n >= sizeof(iso)) n = sizeof(iso) - 1;
      __builtin_memcpy(iso, doc + p + i + 9, n);
      iso[n] = '\0';
      uint32_t q = e + 1;
      while (q < list.from) {
        int j = 0;
        while (doc[q + j] == ' ') ++j;
        if (j <= list.indent && doc[q + j] == '-') break;
        q = lineEndAt(doc, len, q) + 1;
      }
      fn(static_cast<const char*>(iso), p, q, list.indent);
      p = q;
    } else {
      p = e + 1;
    }
  }
}

}  // namespace journalmeta
