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
  if (s.prompts[0]) {
    put(out, outsz, n, over, "%*sprompts: [", in, "");
    for (const char* p = s.prompts; *p; ++p) {
      if (*p == ',') put(out, outsz, n, over, ", ");
      else put(out, outsz, n, over, "%c", *p);
    }
    put(out, outsz, n, over, "]\n");
  }
  if (s.placeTag[0]) {
    char q[80];
    yamlQuote(s.placeTag, q, sizeof(q));
    put(out, outsz, n, over, "%*splace_tag: \"%s\"\n", in, "", q);
  }
  if (s.ambient[0]) put(out, outsz, n, over, "%*sambient: %s\n", in, "", s.ambient);
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
  return findListItem(doc, len, "sessions", "start", startIso, out);
}

uint32_t lineEndAt(const char* s, uint32_t len, uint32_t p) { return lineEnd(s, len, p); }

namespace {

// Offset of the closing "---" line, 0 without front matter.
uint32_t frontMatterClose(const char* doc, uint32_t len) {
  if (len < 8 || strncmp(doc, "---\n", 4) != 0) return 0;
  for (uint32_t p = 4; p + 3 <= len; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, "---", 3) == 0 && (p + 3 == len || doc[p + 3] == '\n' || doc[p + 3] == '\r')) return p;
    if (lineEnd(doc, len, p) >= len) return 0;
  }
  return 0;
}

// Offset of a top-level "key:" line before `close`, 0 if absent.
uint32_t topKey(const char* doc, uint32_t len, uint32_t close, const char* key) {
  const size_t k = strlen(key);
  for (uint32_t p = 4; p < close; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, key, k) == 0 && doc[p + k] == ':') return p;
  }
  return 0;
}

}  // namespace

bool findListItem(const char* doc, uint32_t len, const char* listKey, const char* headKey, const char* headValue,
                  Splice& out) {
  out = Splice();
  const uint32_t close = frontMatterClose(doc, len);
  if (!close) return false;
  const uint32_t key = topKey(doc, len, close, listKey);
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

  // The item: "<indent>- <headKey>: <headValue>", up to the next item at that indent.
  char head[96];
  snprintf(head, sizeof(head), "%*s- %s: %s", out.indent, "", headKey, headValue);
  const size_t headLen = strlen(head);
  for (uint32_t p = listStart; p < listEnd; p = lineEnd(doc, len, p) + 1) {
    if (strncmp(doc + p, head, headLen) == 0 && (doc[p + headLen] == '\n' || doc[p + headLen] == '\r')) {
      uint32_t q = lineEnd(doc, len, p) + 1;
      while (q < listEnd) {
        int i = 0;
        while (doc[q + i] == ' ') ++i;
        if (i <= out.indent && doc[q + i] == '-') break;
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

bool findTopBlock(const char* doc, uint32_t len, const char* key, Splice& out) {
  out = Splice();
  const uint32_t close = frontMatterClose(doc, len);
  if (!close) return false;
  const uint32_t at = topKey(doc, len, close, key);
  if (!at) {
    out.from = out.to = close;
    return true;
  }
  uint32_t end = lineEnd(doc, len, at) + 1;
  while (end < close && doc[end] == ' ') end = lineEnd(doc, len, end) + 1;
  out.from = at;
  out.to = end;
  return true;
}

void findItemLine(const char* doc, uint32_t itemFrom, uint32_t itemTo, int indent, const char* key, Splice& out) {
  out = Splice();
  out.indent = indent;
  const size_t k = strlen(key);
  const int in = indent + 2;
  uint32_t partsAt = 0;
  for (uint32_t p = lineEnd(doc, itemTo, itemFrom) + 1; p < itemTo; p = lineEnd(doc, itemTo, p) + 1) {
    int i = 0;
    while (p + i < itemTo && doc[p + i] == ' ') ++i;
    if (i != in) continue;
    if (strncmp(doc + p + i, key, k) == 0 && doc[p + i + k] == ':') {
      out.from = p;
      out.to = lineEnd(doc, itemTo, p) + 1;
      return;
    }
    if (strncmp(doc + p + i, "parts:", 6) == 0) partsAt = p;
  }
  out.from = out.to = partsAt ? partsAt : itemTo;
}

bool itemValue(const char* doc, uint32_t from, uint32_t to, int indent, const char* key, char* out, size_t outsz) {
  Splice sp;
  findItemLine(doc, from, to, indent, key, sp);
  if (sp.from == sp.to) return false;
  const char* v = doc + sp.from + indent + 2 + strlen(key) + 1;
  const char* e = doc + sp.to;
  while (v < e && *v == ' ') ++v;
  while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) --e;
  size_t n = static_cast<size_t>(e - v);
  if (n >= outsz) n = outsz - 1;
  memcpy(out, v, n);
  out[n] = '\0';
  return true;
}

bool flowLatLon(const char* flow, double& lat, double& lon) {
  const char* a = strstr(flow, "lat:");
  const char* b = strstr(flow, "lon:");
  return a && b && sscanf(a + 4, "%lf", &lat) == 1 && sscanf(b + 4, "%lf", &lon) == 1;
}

bool parseAmbient(const char* text, size_t len, char* out, size_t outsz) {
  if (len < 2 || text[0] != '1' || text[1] != ';') return false;
  size_t o = 0;
  bool any = false;
  auto put = [&](const char* s, size_t n) {
    if (o + n + 2 >= outsz) return false;
    memcpy(out + o, s, n);
    o += n;
    return true;
  };
  put("{", 1);
  size_t i = 2;
  while (i < len) {
    size_t j = i;
    while (j < len && text[j] != ';') ++j;
    const char* eq = static_cast<const char*>(memchr(text + i, '=', j - i));
    if (eq) {
      // key: [a-z0-9_] only
      char key[24];
      size_t kn = 0;
      for (const char* c = text + i; c < eq && kn + 1 < sizeof(key); ++c) {
        const char ch = (*c >= 'A' && *c <= 'Z') ? static_cast<char>(*c + 32) : *c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_') key[kn++] = ch;
      }
      key[kn] = '\0';
      const char* v = eq + 1;
      size_t vn = static_cast<size_t>(text + j - v);
      while (vn && (v[vn - 1] == '\n' || v[vn - 1] == '\r' || v[vn - 1] == ' ')) --vn;
      if (kn && vn) {
        char val[64];
        if (vn >= sizeof(val)) vn = sizeof(val) - 1;
        memcpy(val, v, vn);
        val[vn] = '\0';
        char* end = nullptr;
        strtod(val, &end);
        const bool number = end && *end == '\0';
        char item[112];
        int n;
        if (number) {
          n = snprintf(item, sizeof(item), "%s%s: %s", any ? ", " : "", key, val);
        } else {
          char q[80];
          size_t qo = 0;
          for (size_t c = 0; c < vn && qo + 2 < sizeof(q); ++c) {
            if (static_cast<unsigned char>(val[c]) < 0x20) continue;
            if (val[c] == '"' || val[c] == '\\') q[qo++] = '\\';
            q[qo++] = val[c];
          }
          q[qo] = '\0';
          n = snprintf(item, sizeof(item), "%s%s: \"%s\"", any ? ", " : "", key, q);
        }
        if (n > 0 && put(item, static_cast<size_t>(n))) any = true;
      }
    }
    i = j + 1;
  }
  put("}", 1);
  out[o] = '\0';
  return any;
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
