#include "Links.h"

#include <SDCardManager.h>
#include <stdlib.h>
#include <string.h>

#include "Config.h"
#include "LinkRewrite.h"

namespace Links {

namespace {

constexpr size_t kMaxPath = 232;
constexpr size_t kMaxNote = 48 * 1024;  // larger notes are left alone (RAM)

bool isNote(const char* name) {
  const size_t n = strlen(name);
  return n > 3 && strcmp(name + n - 3, ".md") == 0;
}

// Calls fn(fullPath, name) for every note under `dir`, skipping hidden entries.
template <typename Fn>
void walk(const char* dir, Fn&& fn) {
  FsFile d = SdMan.open(dir, O_RDONLY);
  if (!d || !d.isDirectory()) return;
  char name[96];
  for (FsFile f = d.openNextFile(); f; f = d.openNextFile()) {
    const bool isDir = f.isDirectory();
    f.getName(name, sizeof(name));
    f.close();
    if (name[0] == '.') continue;
    char full[kMaxPath];
    const int n = strcmp(dir, "/") == 0 ? snprintf(full, sizeof(full), "/%s", name)
                                        : snprintf(full, sizeof(full), "%s/%s", dir, name);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(full)) continue;
    if (isDir) walk(full, fn);
    else if (isNote(name)) fn(full, name);
  }
  d.close();
}

// The stem as it may appear URL-encoded ("Old Idea" -> "Old%20Idea").
void encodedStem(const char* stem, char* out, size_t outsz) {
  size_t o = 0;
  for (; *stem && o + 4 < outsz; ++stem) {
    if (*stem == ' ') {
      memcpy(out + o, "%20", 3);
      o += 3;
    } else {
      out[o++] = *stem;
    }
  }
  out[o] = '\0';
}

}  // namespace

Result afterRename(const char* oldFull, const char* newFull) {
  Result res;
  // Root: the notes folder when the note is inside it, else the whole card.
  char root[80];
  const size_t nf = strlen(g_config.notesFolder);
  if (strncmp(oldFull, g_config.notesFolder, nf) == 0 && oldFull[nf] == '/') {
    snprintf(root, sizeof(root), "%s", g_config.notesFolder);
  } else {
    strcpy(root, "/");
  }
  const size_t skip = strcmp(root, "/") == 0 ? 1 : strlen(root) + 1;
  const char* oldRel = oldFull + skip;
  const char* newRel = newFull + skip;

  const char* oldName = strrchr(oldFull, '/') ? strrchr(oldFull, '/') + 1 : oldFull;
  char stem[96];
  snprintf(stem, sizeof(stem), "%s", oldName);
  if (isNote(stem)) stem[strlen(stem) - 3] = '\0';
  char stemEnc[160];
  encodedStem(stem, stemEnc, sizeof(stemEnc));

  // Another note with the old name makes bare [[name]] links ambiguous.
  walk(root, [&](const char*, const char* name) {
    if (strcmp(name, oldName) == 0) res.nameShared = true;
  });
  const linkrewrite::Rename rename{oldRel, newRel, !res.nameShared};

  walk(root, [&](const char* full, const char*) {
    FsFile f = SdMan.open(full, O_RDONLY);
    if (!f) return;
    const size_t size = static_cast<size_t>(f.size());
    if (size > kMaxNote) {
      f.close();
      ++res.skipped;
      return;
    }
    char* text = static_cast<char*>(malloc(size + 1));
    if (!text) {
      f.close();
      ++res.skipped;
      return;
    }
    const int got = f.read(reinterpret_cast<uint8_t*>(text), size);
    f.close();
    text[got > 0 ? got : 0] = '\0';
    // Cheap check before the full pass: does it mention the name at all?
    if (got <= 0 || (!strstr(text, stem) && !strstr(text, stemEnc))) {
      free(text);
      return;
    }

    const size_t cap = size * 2 + 1024;
    char* out = static_cast<char*>(malloc(cap));
    size_t outLen = 0;
    const int changed = out ? linkrewrite::rewrite(text, static_cast<size_t>(got), full + skip, rename, out, cap, outLen) : -1;
    free(text);
    if (changed < 0) {
      ++res.skipped;
    } else if (changed > 0) {
      char tmp[kMaxPath + 8];
      snprintf(tmp, sizeof(tmp), "%s.lnktmp", full);
      FsFile w = SdMan.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
      const bool ok = w && w.write(reinterpret_cast<const uint8_t*>(out), outLen) == outLen;
      if (w) w.close();
      if (ok && SdMan.replaceFile(tmp, full)) {
        res.links = static_cast<uint16_t>(res.links + changed);
        ++res.notes;
      } else {
        SdMan.remove(tmp);
        ++res.skipped;
      }
    }
    free(out);
  });
  return res;
}

}  // namespace Links
