#pragma once

// Journaling prompts: parsing a prompt library and picking from it. Pure (no
// Arduino/SD), host-testable. The format (prompts/library.txt):
//
//   # comment
//   @ category-id | Category name | why this kind of writing helps
//   prompt-id | the prompt | why it helps
//
// Libraries: the built-in one (compiled in), the user's own
// `<notes>/_inkdeck/prompts.txt`, and the server's suggestions
// `<notes>/_inkdeck/prompts-suggested.txt`. Fields point into the library text
// (not NUL-terminated); use copy() to get a C string.

#include <stddef.h>
#include <stdint.h>

namespace prompts {

constexpr const char* kUserPath = "/_inkdeck/prompts.txt";            // under the notes folder
constexpr const char* kSuggestedPath = "/_inkdeck/prompts-suggested.txt";

struct Field {
  const char* p = nullptr;
  uint16_t n = 0;
  bool equals(const char* s) const;
};

struct Category {
  Field id, name, why;
};

struct Prompt {
  Field id, text, why;
  Category category;  // the "@" line above it (empty if none)
};

struct Library {
  const char* text = nullptr;
  size_t len = 0;
};

// Calls fn(ctx, prompt) for each prompt in order; stop by returning false.
using PromptFn = bool (*)(void* ctx, const Prompt& p);
void forEach(const Library& lib, PromptFn fn, void* ctx);
// Calls fn(ctx, category, number of prompts in it) for each category in order.
using CategoryFn = bool (*)(void* ctx, const Category& c, uint16_t count);
void forEachCategory(const Library& lib, CategoryFn fn, void* ctx);

uint16_t count(const Library& lib);
// The index-th prompt overall, or the index-th in `categoryId` (if given).
bool nth(const Library& lib, uint16_t index, Prompt& out, const char* categoryId = nullptr);
bool find(const Library& lib, const char* id, Prompt& out);

// "Today's prompt": the same for everyone on a given day (days since 1970),
// different every day, cycling through the whole library.
uint16_t dailyIndex(int32_t day, uint16_t count);

// NUL-terminated copy (truncated to fit).
void copy(const Field& f, char* out, size_t outsz);
// True for ids made of a-z, 0-9 and '-' (what entries can record).
bool validId(const char* id);

// What goes into the entry at `pos` of `doc`: "> **Prompt:** <text>\n\n",
// preceded by whatever newlines it takes to start a new paragraph there.
size_t entryText(const Prompt& p, const char* doc, uint32_t pos, char* out, size_t outsz);

}  // namespace prompts
