#pragma once

// Rewrites links to a renamed note, following GitJournal's link rules
// (lib/utils/link_resolver.dart), so links keep working on both devices.
// Pure (no Arduino/SD dependencies) and host-testable.
//
// Paths are relative to the notes root, '/'-separated, with the extension
// ("Ideas/old.md"). A rename keeps the note in its folder; only the file name
// changes.
//
// Handled:
//   [[old]]  [[old.md]]  [[old|text]]  [[old#heading]]   bare wiki links: by
//       file name anywhere, so only rewritten when that name is unique
//   [[Ideas/old]]                       wiki links with a path (from the root)
//   [text](old.md)  [text](../Ideas/old.md#x)  [text](</Ideas/old.md>)
//   [label]: old.md                     markdown links/definitions, resolved
//       from the linking note's folder ('/' = the notes root)
// Skipped: front matter, fenced code blocks, inline code, and URLs.

#include <stddef.h>
#include <stdint.h>

namespace linkrewrite {

struct Rename {
  const char* oldPath;     // "Ideas/old.md"
  const char* newPath;     // "Ideas/new.md"
  bool bareNameUnique;     // no other note is called "old.md"
};

// Rewrites `text` (the note at `notePath`) into out[0..outLen). Returns the
// number of links changed, or -1 if `out` is too small.
int rewrite(const char* text, size_t len, const char* notePath, const Rename& r, char* out, size_t outCap,
            size_t& outLen);

}  // namespace linkrewrite
