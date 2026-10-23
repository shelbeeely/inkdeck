#include "Prompts.h"

#include <stdio.h>
#include <string.h>

namespace prompts {

namespace {

Field trimmed(const char* p, const char* e) {
  while (p < e && (*p == ' ' || *p == '\t')) ++p;
  while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) --e;
  Field f;
  f.p = p;
  f.n = static_cast<uint16_t>(e - p);
  return f;
}

// Splits [p, e) at '|' into up to three fields. Returns how many were found.
int split3(const char* p, const char* e, Field out[3]) {
  int n = 0;
  const char* start = p;
  for (const char* q = p; q <= e && n < 3; ++q) {
    if (q == e || (*q == '|' && n < 2)) {
      out[n++] = trimmed(start, q);
      start = q + 1;
    }
  }
  return n;
}

// Walks the library line by line: calls onCategory / onPrompt (either may be null).
template <typename C, typename P>
void walk(const Library& lib, C onCategory, P onPrompt) {
  if (!lib.text) return;
  Category current;
  const char* p = lib.text;
  const char* end = lib.text + lib.len;
  while (p < end) {
    const char* e = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
    if (!e) e = end;
    const Field line = trimmed(p, e);
    if (line.n && line.p[0] != '#') {
      Field f[3];
      if (line.p[0] == '@') {
        const int n = split3(line.p + 1, line.p + line.n, f);
        current = Category();
        if (n >= 1) current.id = f[0];
        current.name = n >= 2 ? f[1] : f[0];
        if (n >= 3) current.why = f[2];
        if (!onCategory(current)) return;
      } else if (split3(line.p, line.p + line.n, f) >= 2 && f[0].n && f[1].n) {
        Prompt pr;
        pr.id = f[0];
        pr.text = f[1];
        pr.why = f[2];
        if (!pr.why.n) pr.why = current.why;
        pr.category = current;
        if (!onPrompt(pr)) return;
      }
    }
    p = e + 1;
  }
}

}  // namespace

bool Field::equals(const char* s) const { return s && strlen(s) == n && memcmp(p, s, n) == 0; }

void forEach(const Library& lib, PromptFn fn, void* ctx) {
  walk(lib, [](const Category&) { return true; }, [&](const Prompt& p) { return fn(ctx, p); });
}

void forEachCategory(const Library& lib, CategoryFn fn, void* ctx) {
  // Count each category's prompts first, then report them in order.
  Category cat;
  uint16_t n = 0;
  bool have = false, stop = false;
  walk(
      lib,
      [&](const Category& c) {
        if (have && !fn(ctx, cat, n)) {
          stop = true;
          return false;
        }
        cat = c;
        n = 0;
        have = true;
        return true;
      },
      [&](const Prompt&) {
        ++n;
        return true;
      });
  if (have && !stop) fn(ctx, cat, n);
}

uint16_t count(const Library& lib) {
  uint16_t n = 0;
  walk(lib, [](const Category&) { return true; }, [&](const Prompt&) {
    ++n;
    return true;
  });
  return n;
}

bool nth(const Library& lib, uint16_t index, Prompt& out, const char* categoryId) {
  uint16_t i = 0;
  bool found = false;
  walk(lib, [](const Category&) { return true; }, [&](const Prompt& p) {
    if (categoryId && !p.category.id.equals(categoryId)) return true;
    if (i++ == index) {
      out = p;
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

bool find(const Library& lib, const char* id, Prompt& out) {
  bool found = false;
  walk(lib, [](const Category&) { return true; }, [&](const Prompt& p) {
    if (!p.id.equals(id)) return true;
    out = p;
    found = true;
    return false;
  });
  return found;
}

uint16_t dailyIndex(int32_t day, uint16_t count) {
  if (!count) return 0;
  // A fixed stride coprime with the count visits every prompt once per cycle,
  // and neighbouring days land in different categories.
  uint32_t stride = 37;
  auto gcd = [](uint32_t a, uint32_t b) {
    while (b) {
      const uint32_t t = a % b;
      a = b;
      b = t;
    }
    return a;
  };
  while (gcd(stride, count) != 1) ++stride;
  const uint32_t d = static_cast<uint32_t>(day < 0 ? -day : day);
  return static_cast<uint16_t>((static_cast<uint64_t>(d) * stride) % count);
}

void copy(const Field& f, char* out, size_t outsz) {
  if (!outsz) return;
  size_t n = f.n < outsz - 1 ? f.n : outsz - 1;
  // Don't cut a UTF-8 character in half.
  if (n < f.n) {
    while (n > 0 && (static_cast<unsigned char>(f.p[n]) & 0xC0) == 0x80) --n;
  }
  if (n) memcpy(out, f.p, n);
  out[n] = '\0';
}

bool validId(const char* id) {
  if (!id || !*id) return false;
  for (const char* p = id; *p; ++p) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
  }
  return true;
}

size_t entryText(const Prompt& p, const char* doc, uint32_t pos, char* out, size_t outsz) {
  const char* lead = "";
  if (pos >= 1 && doc[pos - 1] != '\n') lead = "\n\n";
  else if (pos >= 2 && doc[pos - 2] != '\n') lead = "\n";
  const int n = snprintf(out, outsz, "%s> **Prompt:** %.*s\n\n", lead, p.text.n, p.text.p);
  return n > 0 && static_cast<size_t>(n) < outsz ? static_cast<size_t>(n) : 0;
}

}  // namespace prompts
