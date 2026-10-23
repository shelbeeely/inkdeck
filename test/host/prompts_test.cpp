#include "Prompts.h"
#include "JournalMeta.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
using namespace prompts;
static int fails = 0;
#define CHECK(n, c) do { bool ok = (c); printf("%s %s\n", ok ? "PASS" : "FAIL", n); if (!ok) fails++; } while (0)
static std::string s(const Field& f) { return std::string(f.p, f.n); }
int main(int, char** argv) {
  std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf(); const std::string text = ss.str();
  Library lib{text.data(), text.size()};

  // The real library: every prompt valid, ids unique, every category non-empty with a why.
  std::set<std::string> ids; int bad = 0, total = 0;
  forEach(lib, [](void* c, const Prompt& p) {
    auto* x = static_cast<std::pair<std::set<std::string>*, int*>*>(c);
    char id[40]; copy(p.id, id, sizeof id);
    if (!validId(id) || !x->first->insert(id).second || !p.why.n || !p.category.id.n || p.text.n > 110) {
      printf("  bad prompt: %s (text %u chars)\n", id, p.text.n); ++*x->second;
    }
    return true;
  }, new std::pair<std::set<std::string>*, int*>(&ids, &bad));
  total = count(lib);
  CHECK("library prompts valid, unique, with a why, fit a row", bad == 0);
  CHECK("library size", total >= 80 && (int)ids.size() == total);
  int cats = 0, emptyCats = 0;
  struct C { int* cats; int* empty; } cc{&cats, &emptyCats};
  forEachCategory(lib, [](void* c, const Category& cat, uint16_t n) {
    auto* x = static_cast<C*>(c); ++*x->cats; if (!n || !cat.why.n) ++*x->empty; return true; }, &cc);
  CHECK("categories", cats >= 10 && emptyCats == 0);

  Prompt p;
  CHECK("find", find(lib, "grat-three", p) && s(p.category.id) == "gratitude" && s(p.text).rfind("Three good things", 0) == 0);
  CHECK("nth in category", nth(lib, 1, p, "stress") && s(p.id) == "stress-control");
  CHECK("missing", !find(lib, "nope", p) && !nth(lib, 999, p));

  // Daily: deterministic, differs day to day, and covers the whole library over a cycle.
  std::set<int> seen; for (int d = 0; d < total; ++d) seen.insert(dailyIndex(20000 + d, total));
  CHECK("daily covers every prompt once per cycle", (int)seen.size() == total);
  CHECK("daily changes", dailyIndex(20000, total) != dailyIndex(20001, total));

  // A small user file: category-less prompts, '|' inside the why, CRLF, comments.
  const char* user = "# mine\r\nmy-1 | What made you laugh? \r\n@ work | Work | Because.\nwork-1 | Best meeting? | Fun | really\n";
  Library u{user, strlen(user)};
  CHECK("user count", count(u) == 2);
  CHECK("user prompt w/o category", nth(u, 0, p) && s(p.text) == "What made you laugh?" && !p.why.n);
  CHECK("why keeps '|'", nth(u, 1, p) && s(p.why) == "Fun | really" && s(p.category.name) == "Work");

  // Entry text: always starts a fresh paragraph.
  find(lib, "quick-best", p);
  char out[200];
  const char* doc = "## 09:00\n\n";
  CHECK("entry after blank line", entryText(p, doc, strlen(doc), out, sizeof out) && !strcmp(out, "> **Prompt:** Best part of today?\n\n"));
  doc = "## 09:00\nhello";
  entryText(p, doc, strlen(doc), out, sizeof out);
  CHECK("entry mid-paragraph", !strncmp(out, "\n\n> **Prompt:**", 15));
  doc = "hello\n";
  entryText(p, doc, strlen(doc), out, sizeof out);
  CHECK("entry after one newline", !strncmp(out, "\n> **Prompt:**", 14));

  // UTF-8 safe truncation.
  Field f{"caf\xc3\xa9!", 6}; char small[5]; copy(f, small, sizeof small);
  CHECK("copy keeps whole characters", !strcmp(small, "caf"));

  // The session record lists the prompts.
  journalmeta::Session se; se.opened = 1761235200; se.lastKey = se.opened + 60; se.keys = 10; se.words = 5;
  strcpy(se.prompts, "grat-three,revisit-2025-10-23");
  char y[1024];
  auto iso = [](int64_t, char* o, size_t n) { snprintf(o, n, "2026-10-23T09:00:00-07:00"); };
  auto clk = [](int64_t, char* o, size_t n) { snprintf(o, n, "09:00:00"); };
  size_t n = journalmeta::buildSessionYaml(se, 2, iso, clk, y, sizeof y);
  CHECK("session yaml prompts", n && strstr(y, "    prompts: [grat-three, revisit-2025-10-23]\n"));
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails != 0;
}
