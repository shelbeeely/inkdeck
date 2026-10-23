#include <PromptLibrary.h>
#include <SDCardManager.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Config.h"
#include "Journal.h"
#include "Prompts.h"
#include "UsageStats.h"
#include "WritingStats.h"
#include "screens.h"

// The prompt picker: shown over the editor (Cmd/Ctrl+Shift+P, or when Today
// opens). Lists today's prompt, the server's suggestions, "Surprise me" and
// the categories; inside a category, its prompts. The selected row's "why it
// helps" line is shown at the bottom. Choosing one puts it into the entry as
// a quote and records its id in the session (WritingStats).

using namespace freeink::ui;

namespace {

enum class Source : uint8_t { BuiltIn, User, Suggested };
enum class Kind : uint8_t { Prompt, Category, Surprise };

struct Row {
  Kind kind;
  Source source;
  uint16_t index;      // prompt index within its source (or category)
  char category[24];   // category id (Kind::Category, or the prompt's category when browsing one)
};

struct Picker {
  static constexpr int kMaxRows = 40;
  char* user = nullptr;
  size_t userLen = 0;
  char* suggested = nullptr;
  size_t suggestedLen = 0;
  bool inCategory = false;
  Source catSource = Source::BuiltIn;
  char catId[24] = {0};
  char catName[40] = {0};
  int sel = 0, topSel = 0;
  uint16_t top = 0, visible = 1;
  uint16_t rowCount = 0;
  Row rows[kMaxRows];
  ListItem items[kMaxRows];
  char labels[kMaxRows][112];
  char values[kMaxRows][8];
};

Picker* g_p = nullptr;

prompts::Library lib(Source s) {
  prompts::Library l;
  if (s == Source::BuiltIn) {
    l.text = kPromptLibrary;
    l.len = sizeof(kPromptLibrary) - 1;
  } else if (s == Source::User) {
    l.text = g_p->user;
    l.len = g_p->userLen;
  } else {
    l.text = g_p->suggested;
    l.len = g_p->suggestedLen;
  }
  return l;
}

char* readFile(const char* rel, size_t maxLen, size_t& len) {
  len = 0;
  char path[128];
  snprintf(path, sizeof(path), "%s%s", g_config.notesFolder, rel);
  FsFile f = SdMan.open(path, O_RDONLY);
  if (!f) return nullptr;
  char* buf = new char[maxLen];
  if (buf) {
    const int n = f.read(reinterpret_cast<uint8_t*>(buf), maxLen);
    len = n > 0 ? static_cast<size_t>(n) : 0;
  }
  f.close();
  if (!len) {
    delete[] buf;
    return nullptr;
  }
  return buf;
}

int32_t today() {
  const time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return usage::daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

Row& addRow(Kind kind, Source source, uint16_t index, const char* label, const char* heading = nullptr) {
  Picker& p = *g_p;
  const int i = p.rowCount++;
  Row& r = p.rows[i];
  r = Row{kind, source, index, {0}};
  snprintf(p.labels[i], sizeof(p.labels[i]), "%s", label);
  p.values[i][0] = '\0';
  p.items[i] = ListItem{};
  p.items[i].label = p.labels[i];
  p.items[i].value = nullptr;  // set with a count for categories
  p.items[i].actionValue = static_cast<int16_t>(i);
  p.items[i].sectionHeading = heading;
  return r;
}

void buildTop() {
  Picker& p = *g_p;
  p.rowCount = 0;
  p.inCategory = false;
  char text[112];
  prompts::Prompt pr;

  const prompts::Library built = lib(Source::BuiltIn);
  const uint16_t total = prompts::count(built);
  if (prompts::nth(built, prompts::dailyIndex(today(), total), pr)) {
    prompts::copy(pr.text, text, sizeof(text));
    addRow(Kind::Prompt, Source::BuiltIn, prompts::dailyIndex(today(), total), text, "Today's prompt");
  }
  const prompts::Library sugg = lib(Source::Suggested);
  for (uint16_t i = 0; i < 6 && prompts::nth(sugg, i, pr) && p.rowCount < Picker::kMaxRows; ++i) {
    prompts::copy(pr.text, text, sizeof(text));
    addRow(Kind::Prompt, Source::Suggested, i, text, i == 0 ? "For you" : nullptr);
  }
  addRow(Kind::Surprise, Source::BuiltIn, 0, "Surprise me", "Browse");

  struct Ctx {
    Source source;
    const char* heading;
  };
  auto addCategories = [](Source source, const char* heading) {
    Ctx ctx{source, heading};
    prompts::forEachCategory(lib(source), [](void* c, const prompts::Category& cat, uint16_t n) {
      auto* x = static_cast<Ctx*>(c);
      if (!n || g_p->rowCount >= Picker::kMaxRows) return g_p->rowCount < Picker::kMaxRows;
      char name[48];
      prompts::copy(cat.name, name, sizeof(name));
      Row& r = addRow(Kind::Category, x->source, 0, name, x->heading);
      x->heading = nullptr;
      prompts::copy(cat.id, r.category, sizeof(r.category));
      const int row = g_p->rowCount - 1;
      snprintf(g_p->values[row], sizeof(g_p->values[0]), "%u", static_cast<unsigned>(n));
      g_p->items[row].value = g_p->values[row];
      return true;
    }, &ctx);
  };
  addCategories(Source::BuiltIn, nullptr);
  if (p.user) addCategories(Source::User, "Your prompts");
}

void buildCategory(const Row cat) {  // a copy: the rows it came from are rebuilt here
  Picker& p = *g_p;
  p.inCategory = true;
  p.catSource = cat.source;
  snprintf(p.catId, sizeof(p.catId), "%s", cat.category);
  snprintf(p.catName, sizeof(p.catName), "%s", p.labels[p.sel]);
  p.rowCount = 0;
  prompts::Prompt pr;
  char text[112];
  const prompts::Library l = lib(cat.source);
  for (uint16_t i = 0; p.rowCount < Picker::kMaxRows && prompts::nth(l, i, pr, p.catId); ++i) {
    prompts::copy(pr.text, text, sizeof(text));
    Row& r = addRow(Kind::Prompt, cat.source, i, text);
    snprintf(r.category, sizeof(r.category), "%s", p.catId);
  }
  p.sel = 0;
  p.top = 0;
}

// The prompt a row stands for (prompt rows only).
bool rowPrompt(const Row& r, prompts::Prompt& out) {
  return prompts::nth(lib(r.source), r.index, out, r.category[0] ? r.category : nullptr);
}

void choose(const prompts::Prompt& pr) {
  AppState& st = g_state;
  Document& d = st.doc;
  char buf[320];
  const uint32_t c = d.cursor();
  const size_t n = prompts::entryText(pr, d.text(), c, buf, sizeof(buf));
  if (n && d.replaceRange(c, c, buf, static_cast<uint32_t>(n))) {
    d.setCursor(c + static_cast<uint32_t>(n));
    char id[40];
    prompts::copy(pr.id, id, sizeof(id));
    if (prompts::validId(id)) WritingStats::addPrompt(id);
    // A fresh session with only a prompt in it is still "untouched": leaving
    // without writing drops it, heading and all.
    if (st.journalStub) st.journalStub = Journal::stubHash(d);
  } else {
    showToast("No room for the prompt");
  }
  promptsClose();
}

}  // namespace

bool promptsOpen() {
  if (!g_p) g_p = new Picker();
  if (!g_p) return false;
  g_p->user = readFile(prompts::kUserPath, 6144, g_p->userLen);
  g_p->suggested = readFile(prompts::kSuggestedPath, 3072, g_p->suggestedLen);
  buildTop();
  g_p->sel = 0;
  g_p->top = 0;
  uiGotoScreen(AppScreen::Prompts);
  return true;
}

void promptsClose() {
  if (g_p) {
    delete[] g_p->user;
    delete[] g_p->suggested;
    delete g_p;
    g_p = nullptr;
  }
  uiGotoScreen(AppScreen::Editor);
}

void promptsNav(int delta) {
  if (!g_p || !g_p->rowCount) return;
  g_p->sel = (g_p->sel + delta + g_p->rowCount) % g_p->rowCount;
  uiInvalidate(false);
}

void promptsConfirm() {
  if (!g_p || !g_p->rowCount) return;
  Picker& p = *g_p;
  const Row& r = p.rows[p.sel];
  prompts::Prompt pr;
  switch (r.kind) {
    case Kind::Category:
      p.topSel = p.sel;
      buildCategory(r);
      uiInvalidate(true);
      return;
    case Kind::Surprise: {
      const prompts::Library l = lib(Source::BuiltIn);
      const uint16_t total = prompts::count(l);
      const uint32_t x = static_cast<uint32_t>(millis()) * 2654435761u ^ static_cast<uint32_t>(time(nullptr));
      if (total && prompts::nth(l, static_cast<uint16_t>((x >> 8) % total), pr)) choose(pr);
      return;
    }
    case Kind::Prompt:
      if (rowPrompt(r, pr)) choose(pr);
      return;
  }
}

void promptsBack() {
  if (g_p && g_p->inCategory) {
    buildTop();
    g_p->sel = g_p->topSel;
    g_p->top = 0;
    uiInvalidate(true);
    return;
  }
  promptsClose();
}

void promptsScreen(ScreenT& s, void*) {
  if (!g_p) return;
  Picker& p = *g_p;
  s.header(p.inCategory ? p.catName : "Pick a prompt", nullptr, "");
  uiDrawHardwareFooter("Back", "Choose", "Up", "Down");

  Rect body = s.body();
  auto& target = s.frame().target();
  TextStyle full = g_theme.bodyText;
  full.maxLines = 3;
  TextStyle why = g_theme.smallText;
  why.maxLines = 3;
  const int16_t bodyLh = target.lineHeight(full.font);
  const int16_t smallLh = target.lineHeight(why.font);
  const int16_t panelH = static_cast<int16_t>(bodyLh * 3 + smallLh * 3 + 28);

  // Rows grow to two lines of prompt text; the full prompt is in the panel below.
  ListProps lp{};
  lp.items = p.items;
  lp.count = p.rowCount;
  lp.selectedIndex = static_cast<int16_t>(p.sel);
  lp.selectionMarker = SelectionMarker::Triangle;
  lp.labelText = g_theme.bodyText;
  lp.labelText.maxLines = 2;
  Rect listArea = body;
  listArea.height = static_cast<int16_t>(body.height - panelH);
  p.visible = listVisibleRows(listArea, static_cast<int16_t>(bodyLh * 2 + 14));
  p.top = listTopIndexFor(p.sel, p.top, p.visible, p.rowCount);
  lp.topIndex = p.top;
  s.list(lp, listArea.height);

  // The selected prompt in full, and why it helps: the answer to "what's in it for me".
  char text[200] = {0}, whyText[200] = {0};
  const Row& r = p.rows[p.sel];
  prompts::Prompt pr;
  if (r.kind == Kind::Prompt && rowPrompt(r, pr)) {
    prompts::copy(pr.text, text, sizeof(text));
    char w[180];
    prompts::copy(pr.why, w, sizeof(w));
    if (w[0]) snprintf(whyText, sizeof(whyText), "Why: %s", w);
  } else if (r.kind == Kind::Category) {
    snprintf(text, sizeof(text), "%s", p.labels[p.sel]);
    prompts::forEachCategory(lib(r.source), [](void* ctx, const prompts::Category& c, uint16_t) {
      auto* out = static_cast<char*>(ctx);
      char id[24];
      prompts::copy(c.id, id, sizeof(id));
      if (strcmp(id, g_p->rows[g_p->sel].category) != 0) return true;
      char w[180];
      prompts::copy(c.why, w, sizeof(w));
      snprintf(out, 200, "Why: %s", w);
      return false;
    }, whyText);
  } else if (r.kind == Kind::Surprise) {
    snprintf(text, sizeof(text), "Surprise me");
    snprintf(whyText, sizeof(whyText), "Any prompt from the library. Not sure where to start? This is a fine place.");
  }
  int16_t y = static_cast<int16_t>(body.bottom() - panelH);
  target.fill(Rect{body.x, static_cast<int16_t>(y + 4), body.width, 1}, Paint::solid(Color::Black));
  const int16_t x = static_cast<int16_t>(body.x + 6), w = static_cast<int16_t>(body.width - 12);
  y = static_cast<int16_t>(y + 12);
  // text() centres vertically in its rect, so each rect is exactly the text's height.
  auto block = [&](const char* t, const TextStyle& st, int16_t lh) {
    if (!t[0]) return;
    int16_t h = measureWrappedText(target, t, st, w).height;
    if (h < lh) h = lh;
    if (h > lh * st.maxLines) h = static_cast<int16_t>(lh * st.maxLines);
    target.text(Rect{x, y, w, h}, t, st);
    y = static_cast<int16_t>(y + h + 8);
  };
  block(text, full, bodyLh);
  block(whyText, why, smallLh);
}
