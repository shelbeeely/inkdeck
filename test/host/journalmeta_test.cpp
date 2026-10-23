#include "JournalMeta.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <ctime>
using namespace journalmeta;
static void iso(int64_t t, char* o, size_t n) { time_t tt = t; struct tm m; localtime_r(&tt, &m); char b[24], z[8]; strftime(b, 24, "%Y-%m-%dT%H:%M:%S", &m); strftime(z, 8, "%z", &m); snprintf(o, n, "%s%.3s:%.2s", b, z, z + 3); }
static void clk(int64_t t, char* o, size_t n) { time_t tt = t; struct tm m; localtime_r(&tt, &m); strftime(o, n, "%H:%M:%S", &m); }
static void applySession(std::string& doc, const Session& s) {
  char start[32]; iso(s.opened, start, sizeof start);
  Splice sp; if (!findSessionBlock(doc.data(), doc.size(), start, sp)) { puts("NO FRONT MATTER"); return; }
  char y[4096]; size_t n = buildSessionYaml(s, sp.indent, iso, clk, y, sizeof y);
  std::string rep = (sp.needsKey ? "sessions:\n" : "") + std::string(y, n);
  doc.replace(sp.from, sp.to - sp.from, rep);
}
int main(int argc, char** argv) {
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1); tzset();
  Fix f; const char* v = "1;45.5231;-122.6765;12.4;1761255110;Portland, \"Pearl\" District";
  bool ok = parseLocation(v, strlen(v), f);
  printf("parse ok=%d lat=%.4f acc=%u place=%s\n", ok, f.lat, f.accuracyM, f.place);
  Fix bad; printf("bad versions rejected=%d %d %d\n", !parseLocation("2;1;2;3;1761255110;x", 22, bad), !parseLocation("1;91;0;3;1761255110", 19, bad), !parseLocation("1;abc;0;3;1761255110", 20, bad));
  std::string doc = "---\ncreated: 2026-10-23T09:00:00-07:00\nmodified: 2026-10-23T09:00:00-07:00\ntype: Journal\n---\n\n## 09:00\n\nHello world.\n";
  Session a; a.opened = 1761235200; a.lastKey = a.opened + 300; a.activeMs = 250000; a.keys = 900; a.words = 160; a.location = f;
  a.parts[0] = {a.opened + 5, a.opened + 120, 100000, 60}; a.partCount = 1;
  applySession(doc, a);             // first save: adds "sessions:"
  a.parts[1] = {a.opened + 130, a.opened + 300, 150000, 100}; a.partCount = 2; a.lastKey = a.opened + 300;
  applySession(doc, a);             // second save: replaces the same item
  Session b; b.opened = a.opened + 3600; b.lastKey = b.opened + 60; b.activeMs = 50000; b.keys = 200; b.words = 30;
  applySession(doc, b);             // new session: appended
  // GitJournal-style reserialization with "- " at column 0, then a third session.
  // Dedent the sessions list by two spaces (a valid "- " at column 0 style).
  std::string flat; { bool in = false; size_t pos = 0;
    while (pos < doc.size()) { size_t e = doc.find('\n', pos); if (e == std::string::npos) e = doc.size();
      std::string line = doc.substr(pos, e - pos);
      if (line == "sessions:") in = true; else if (line == "---") in = false;
      else if (in && line.rfind("  ", 0) == 0) line = line.substr(2);
      flat += line + "\n"; pos = e + 1; } }
  Session c; c.opened = b.opened + 7200; c.lastKey = c.opened + 10; c.keys = 5; c.words = 1;
  applySession(flat, c);
  FILE* o = fopen(argv[1], "w"); fputs(doc.c_str(), o); fclose(o);
  o = fopen(argv[2], "w"); fputs(flat.c_str(), o); fclose(o);
  // words / paragraphs
  const char* t = "one two\n\nthree four five\nsix\n\nseven";
  uint32_t a0, a1; paragraphBounds(t, strlen(t), 12, a0, a1);
  printf("paragraph words=%u (expect 4), body start=%u\n", countWords(t, a0, a1), bodyStart(doc.data(), doc.size()));
}
