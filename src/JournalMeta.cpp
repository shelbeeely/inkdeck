#include "JournalMeta.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace journalmeta {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }

// Next ';'-separated field of [p, end) into out; advances p. The last field
// (place) takes the rest of the input.
bool field(const char*& p, const char* end, char* out, size_t outsz, bool rest = false) {
  const char* q = p;
  while (q < end && (rest || *q != ';')) ++q;
  size_t n = static_cast<size_t>(q - p);
  while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) --n;
  if (n >= outsz) n = outsz - 1;
  memcpy(out, p, n);
  out[n] = '\0';
  p = q < end ? q + 1 : end;
  return true;
}

bool parseDouble(const char* s, double& out) {
  char* end = nullptr;
  out = strtod(s, &end);
  return end && end != s && *end == '\0';
}

// Appends printf output to out[*n], tracking overflow.
void put(char* out, size_t outsz, size_t& n, bool& over, const char* fmt, ...) __attribute__((format(printf, 5, 6)));
void put(char* out, size_t outsz, size_t& n, bool& over, const char* fmt, ...) {
  if (over) return;
  va_list ap;
  va_start(ap, fmt);
  const int w = vsnprintf(out + n, outsz - n, fmt, ap);
  va_end(ap);
  if (w < 0 || static_cast<size_t>(w) >= outsz - n) {
    over = true;
    return;
  }
  n += static_cast<size_t>(w);
}

// YAML double-quoted scalar body: escape backslash and quote, drop controls.
void yamlQuote(const char* in, char* out, size_t outsz) {
  size_t o = 0;
  for (; *in && o + 2 < outsz; ++in) {
    const unsigned char c = static_cast<unsigned char>(*in);
    if (c < 0x20) continue;
    if (c == '"' || c == '\\') out[o++] = '\\';
    out[o++] = static_cast<char>(c);
  }
  out[o] = '\0';
}

// Line containing offset `p`: [start, end) without the '\n'.
uint32_t lineEnd(const char* s, uint32_t len, uint32_t p) {
  while (p < len && s[p] != '\n') ++p;
  return p;
}

}  // namespace

uint32_t Counters::edit(uint32_t ms, int64_t now, uint32_t inserted, uint32_t removed, bool revision) {
  uint32_t credit = 0;
  if (keys) {
    const uint32_t gap = ms - lastKeyMs;
    if (gap <= kIdleGapMs) {
      credit = gap;
      activeMs += gap;
    } else {
      ++pauses;
      if (gap > longestPauseMs) longestPauseMs = gap;
    }
    burstMs = gap < kBurstGapMs ? burstMs + gap : 0;
    if (burstMs > longestBurstMs) longestBurstMs = burstMs;
  } else {
    firstKey = now;
  }
  ++keys;
  typed += inserted;
  deleted += removed;
  if (revision) ++revisions;
  lastKey = now;
  lastKeyMs = ms;
  return credit;
}

uint32_t wordsPerMinute(int32_t words, uint32_t activeMs) {
  if (words <= 0 || activeMs < 30000) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(words) * 60000 + activeMs / 2) / activeMs);
}

bool parseLocation(const char* text, size_t len, Fix& out) {
  out = Fix();
  const char* p = text;
  const char* end = text + len;
  char version[8], lat[24], lon[24], acc[16], when[24];
  field(p, end, version, sizeof(version));
  if (strcmp(version, "1") != 0) return false;
  field(p, end, lat, sizeof(lat));
  field(p, end, lon, sizeof(lon));
  field(p, end, acc, sizeof(acc));
  field(p, end, when, sizeof(when));
  field(p, end, out.place, sizeof(out.place), true);
  double a = 0, t = 0;
  if (!parseDouble(lat, out.lat) || !parseDouble(lon, out.lon) || !parseDouble(acc, a) || !parseDouble(when, t)) {
    out = Fix();
    return false;
  }
  if (out.lat < -90 || out.lat > 90 || out.lon < -180 || out.lon > 180 || a < 0 || t < 1e9) {
    out = Fix();
    return false;
  }
  out.accuracyM = static_cast<uint32_t>(a + 0.5);
  out.time = static_cast<int64_t>(t);
  strcpy(out.source, "phone");
  out.valid = true;
  return true;
}

size_t buildSessionYaml(const Session& s, int indent, FormatIso iso, FormatClock clock, char* out, size_t outsz) {
  size_t n = 0;
  bool over = false;
  char a[32], b[32];
  const int in = indent + 2;  // keys of the item
  iso(s.opened, a, sizeof(a));
  iso(s.lastKey ? s.lastKey : s.opened, b, sizeof(b));
  put(out, outsz, n, over, "%*s- start: %s\n", indent, "", a);
  put(out, outsz, n, over, "%*send: %s\n", in, "", b);
  put(out, outsz, n, over, "%*sactive_s: %lu\n", in, "", static_cast<unsigned long>(s.activeMs / 1000));
  put(out, outsz, n, over, "%*skeys: %lu\n", in, "", static_cast<unsigned long>(s.keys));
  put(out, outsz, n, over, "%*swords: %ld\n", in, "", static_cast<long>(s.words));
  const Counters& c = s.c;
  if (c.keys) {
    put(out, outsz, n, over, "%*swpm: %lu\n", in, "", static_cast<unsigned long>(wordsPerMinute(s.words, s.activeMs)));
    put(out, outsz, n, over, "%*styped: %lu\n%*sdeleted: %lu\n", in, "", static_cast<unsigned long>(c.typed), in, "",
        static_cast<unsigned long>(c.deleted));
    put(out, outsz, n, over, "%*spauses: %lu\n%*slongest_pause_s: %lu\n", in, "", static_cast<unsigned long>(c.pauses),
        in, "", static_cast<unsigned long>(c.longestPauseMs / 1000));
    put(out, outsz, n, over, "%*slongest_burst_s: %lu\n", in, "", static_cast<unsigned long>(c.longestBurstMs / 1000));
    put(out, outsz, n, over, "%*sfirst_key_s: %lld\n", in, "", static_cast<long long>(c.firstKey - s.opened));
    put(out, outsz, n, over, "%*srevisions: %lu\n", in, "", static_cast<unsigned long>(c.revisions));
  }
  if (s.location.valid) {
    iso(s.location.time, a, sizeof(a));
    put(out, outsz, n, over, "%*slocation: {lat: %.6f, lon: %.6f, accuracy_m: %lu, time: %s, source: %s", in, "",
        s.location.lat, s.location.lon, static_cast<unsigned long>(s.location.accuracyM), a, s.location.source);
    if (s.location.place[0]) {
      char q[80];
      yamlQuote(s.location.place, q, sizeof(q));
      put(out, outsz, n, over, ", place: \"%s\"", q);
    }
    put(out, outsz, n, over, "}\n");
  }
  if (s.partCount) {
    put(out, outsz, n, over, "%*sparts:\n", in, "");
    for (uint8_t i = 0; i < s.partCount; ++i) {
      const Part& p = s.parts[i];
      clock(p.start, a, sizeof(a));
      clock(p.end, b, sizeof(b));
      put(out, outsz, n, over, "%*s- {start: \"%s\", end: \"%s\", active_s: %lu, words: %lu}\n", in + 2, "", a, b,
          static_cast<unsigned long>(p.activeMs / 1000), static_cast<unsigned long>(p.words));
    }
  }
  return over ? 0 : n;
}

bool findSessionBlock(const char* doc, uint32_t len, const char* startIso, Splice& out) {
  out = Splice();
  if (len < 8 || strncmp(doc, "---\n", 4) != 0) return false;
  // Closing "---" line.
  uint32_t close = 0;
  for (uint32_t p = 4; p + 3 <= len; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, "---", 3) == 0 && (p + 3 == len || doc[p + 3] == '\n' || doc[p + 3] == '\r')) {
      close = p;
      break;
    }
    if (lineEnd(doc, len, p) >= len) return false;
  }
  if (!close) return false;

  // "sessions:" at column 0.
  uint32_t key = 0;
  for (uint32_t p = 4; p < close; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, "sessions:", 9) == 0) {
      key = p;
      break;
    }
  }
  if (!key) {
    out.from = out.to = close;
    out.needsKey = true;
    return true;
  }

  // The list: following lines that are indented or start with '-'.
  const uint32_t listStart = lineEnd(doc, len, key) + 1;
  uint32_t listEnd = listStart;
  int indent = -1;
  for (uint32_t p = listStart; p < close; p = lineEnd(doc, len, p) + 1) {
    if (doc[p] != ' ' && doc[p] != '-') break;
    if (indent < 0) {
      int i = 0;
      while (doc[p + i] == ' ') ++i;
      if (doc[p + i] == '-') indent = i;
    }
    listEnd = lineEnd(doc, len, p) + 1;
  }
  out.indent = indent < 0 ? 2 : indent;

  // Our item: "<indent>- start: <startIso>", up to the next item at that indent.
  char head[64];
  snprintf(head, sizeof(head), "%*s- start: %s", out.indent, "", startIso);
  const size_t headLen = strlen(head);
  for (uint32_t p = listStart; p < listEnd; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, head, headLen) == 0 && (doc[p + headLen] == '\n' || doc[p + headLen] == '\r')) {
      uint32_t q = lineEnd(doc, len, p) + 1;
      while (q < listEnd) {
        int i = 0;
        while (doc[q + i] == ' ') ++i;
        if (i <= out.indent && doc[q + i] == '-' ) break;
        q = lineEnd(doc, len, q) + 1;
      }
      out.from = p;
      out.to = q;
      return true;
    }
  }
  out.from = out.to = listEnd;
  return true;
}

uint32_t countWords(const char* s, uint32_t from, uint32_t to) {
  uint32_t words = 0;
  bool in = false;
  for (uint32_t i = from; i < to; ++i) {
    const bool sp = isSpace(s[i]);
    if (!sp && !in) ++words;
    in = !sp;
  }
  return words;
}

void paragraphBounds(const char* s, uint32_t len, uint32_t pos, uint32_t& from, uint32_t& to) {
  if (pos > len) pos = len;
  from = pos;
  while (from > 0 && !(s[from - 1] == '\n' && (from < 2 || s[from - 2] == '\n'))) --from;
  to = pos;
  while (to < len && !(s[to] == '\n' && (to + 1 >= len || s[to + 1] == '\n'))) ++to;
}

uint32_t bodyStart(const char* doc, uint32_t len) {
  if (len < 8 || strncmp(doc, "---\n", 4) != 0) return 0;
  for (uint32_t p = 4; p + 3 <= len; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, "---", 3) == 0 && (p + 3 == len || doc[p + 3] == '\n' || doc[p + 3] == '\r')) {
      const uint32_t e = lineEnd(doc, len, p);
      return e < len ? e + 1 : len;
    }
    if (lineEnd(doc, len, p) >= len) return 0;
  }
  return 0;
}

}  // namespace journalmeta
