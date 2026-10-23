#include "WritingStats.h"

#include <string.h>
#include <time.h>

#include "Clock.h"

namespace WritingStats {

namespace {

bool g_active = false;
journalmeta::Session g_session;
uint32_t g_wordsAtStart = 0;
bool g_partOpen = false;
char g_yaml[4096];

uint32_t bodyWords(const Document& doc) {
  const char* t = doc.text();
  const uint32_t n = doc.length();
  return journalmeta::countWords(t, journalmeta::bodyStart(t, n), n);
}

void isoAt(int64_t t, char* out, size_t outsz) { Clock::formatIsoAt(t, out, outsz); }
void clockAt(int64_t t, char* out, size_t outsz) { Clock::formatClockAt(t, out, outsz); }

}  // namespace

void begin(const Document& doc) {
  g_session = journalmeta::Session();
  g_session.opened = static_cast<int64_t>(time(nullptr));
  g_session.c.opened = g_session.opened;
  g_wordsAtStart = bodyWords(doc);
  g_partOpen = false;
  g_active = true;
}

bool active() { return g_active; }

void onEdit(const Document& doc, uint32_t inserted, uint32_t removed) {
  if (!g_active) return;
  journalmeta::Session& s = g_session;
  const int64_t now = static_cast<int64_t>(time(nullptr));
  const char* t = doc.text();
  const uint32_t len = doc.length();
  const uint32_t c = doc.cursor();

  // Editing a paragraph with more text after it is revising earlier writing.
  uint32_t from, to;
  journalmeta::paragraphBounds(t, len, c, from, to);
  uint32_t rest = to;
  while (rest < len && (t[rest] == '\n' || t[rest] == ' ')) ++rest;
  const bool revision = rest < len;

  const uint32_t credit = s.c.edit(millis(), now, inserted, removed, revision);
  if (g_partOpen) s.parts[s.partCount - 1].activeMs += credit;

  // A blank line just made ends the paragraph being written.
  if (c >= 2 && t[c - 1] == '\n' && t[c - 2] == '\n') {
    g_partOpen = false;
    return;
  }
  // The first character typed after that starts the next part.
  const bool typedText = inserted && c > 0 && t[c - 1] != ' ' && t[c - 1] != '\n';
  if (!g_partOpen && typedText && !revision && s.partCount < journalmeta::Session::kMaxParts) {
    journalmeta::Part& p = s.parts[s.partCount++];
    p = journalmeta::Part();
    p.start = now;
    g_partOpen = true;
  }
  if (g_partOpen && !revision) {
    journalmeta::Part& p = s.parts[s.partCount - 1];
    p.end = now;
    p.words = journalmeta::countWords(t, from, to);
  }
}

void setLocation(const journalmeta::Fix& fix) {
  if (g_active) g_session.location = fix;
}

void writeInto(Document& doc) {
  if (!g_active || g_session.c.keys == 0 || !Clock::valid()) return;
  journalmeta::Session& s = g_session;
  s.words = static_cast<int32_t>(bodyWords(doc)) - static_cast<int32_t>(g_wordsAtStart);
  s.activeMs = s.c.activeMs;
  s.keys = s.c.keys;
  s.lastKey = s.c.lastKey;

  char startIso[32];
  Clock::formatIsoAt(g_session.opened, startIso, sizeof(startIso));
  journalmeta::Splice sp;
  if (!journalmeta::findSessionBlock(doc.text(), doc.length(), startIso, sp)) return;
  size_t n = 0;
  if (sp.needsKey) {
    memcpy(g_yaml, "sessions:\n", 10);
    n = 10;
  }
  const size_t item = journalmeta::buildSessionYaml(g_session, sp.indent, isoAt, clockAt, g_yaml + n, sizeof(g_yaml) - n);
  if (!item) return;  // too many parts to fit; keep the last good record
  doc.replaceRange(sp.from, sp.to, g_yaml, static_cast<uint32_t>(n + item));
}

void end() { g_active = false; }

}  // namespace WritingStats
