#include "JournalMeta.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace journalmeta;
static int fails = 0;
#define CHECK(n, c) do { bool ok = (c); printf("%s %s\n", ok ? "PASS" : "FAIL", n); if (!ok) fails++; } while (0)
static void splice(std::string& d, const Splice& sp, const std::string& text) { d.replace(sp.from, sp.to - sp.from, text); }
static void enrich(std::string& doc) {
  // Weather line per session (replace if present), collected first, applied from the end.
  struct Edit { Splice sp; std::string text; };
  std::vector<Edit> edits;
  forEachSession(doc.data(), doc.size(), [&](const char* iso, uint32_t from, uint32_t to, int indent) {
    char loc[200]; double lat = 0, lon = 0;
    bool hasLoc = itemValue(doc.data(), from, to, indent, "location", loc, sizeof loc) && flowLatLon(loc, lat, lon);
    Splice sp; findItemLine(doc.data(), from, to, indent, "weather", sp);
    char line[160]; snprintf(line, sizeof line, "%*sweather: {temp_c: 11.2, for: \"%s\", lat: %.1f}\n", indent + 2, "", iso, hasLoc ? lat : 0.0);
    edits.push_back({sp, line});
  });
  for (auto it = edits.rbegin(); it != edits.rend(); ++it) splice(doc, it->sp, it->text);
  Splice day; findTopBlock(doc.data(), doc.size(), "day", day);
  splice(doc, day, "day:\n  sunrise: \"07:34\"\n  final: true\n");
}
int main(int, char** argv) {
  std::string doc =
    "---\ncreated: 2026-10-23T09:00:00-07:00\ntype: Journal\nsessions:\n"
    "  - start: 2026-10-23T07:00:00-07:00\n    end: 2026-10-23T07:20:00-07:00\n    words: 400\n"
    "    location: {lat: 45.523100, lon: -122.676500, accuracy_m: 12, place: \"Home\"}\n"
    "    parts:\n      - {start: \"07:00:05\", end: \"07:10:00\", active_s: 500, words: 220}\n"
    "  - start: 2026-10-23T21:00:00-07:00\n    end: 2026-10-23T21:05:00-07:00\n    words: 50\n"
    "---\n\n## 07:00\n\nText.\n";
  int count = 0; forEachSession(doc.data(), doc.size(), [&](const char*, uint32_t, uint32_t, int) { ++count; });
  CHECK("two sessions walked", count == 2);
  enrich(doc);
  std::string once = doc;
  enrich(doc);   // a second Sync must not duplicate anything
  CHECK("idempotent", doc == once);
  // Check-in appended to a new "checkins:" list, then a second one.
  Splice ck; findListItem(doc.data(), doc.size(), "checkins", "time", "\x01", ck);
  splice(doc, ck, std::string(ck.needsKey ? "checkins:\n" : "") + "  - {time: 2026-10-23T07:05:00-07:00, kind: morning, sleep_h: 7.5}\n");
  findListItem(doc.data(), doc.size(), "checkins", "time", "\x01", ck);
  splice(doc, ck, std::string(ck.needsKey ? "checkins:\n" : "") + "  - {time: 2026-10-23T21:30:00-07:00, kind: evening, energy: 4}\n");
  char amb[200]; const char* a = "1;lux=320;noise_db=48.5;Noise=coffee \"shop\";bad key!=x";
  CHECK("ambient", parseAmbient(a, strlen(a), amb, sizeof amb) && !strcmp(amb, "{lux: 320, noise_db: 48.5, noise: \"coffee \\\"shop\\\"\", badkey: \"x\"}"));
  printf("  %s\n", amb);
  FILE* o = fopen(argv[1], "w"); fputs(doc.c_str(), o); fclose(o);
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
}
