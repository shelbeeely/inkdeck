#include "LinkRewrite.h"

#include <string.h>

namespace linkrewrite {

namespace {

constexpr size_t kPath = 256;

struct Out {
  char* buf;
  size_t cap;
  size_t len = 0;
  bool over = false;
  void put(const char* s, size_t n) {
    if (over || len + n > cap) {
      over = true;
      return;
    }
    memcpy(buf + len, s, n);
    len += n;
  }
};

bool endsWith(const char* s, size_t n, const char* suffix) {
  const size_t k = strlen(suffix);
  return n >= k && memcmp(s + n - k, suffix, k) == 0;
}

// "a/./b/../c" -> "a/c"; leading '/' dropped. False if it climbs above root.
bool normalize(const char* in, size_t n, char* out, size_t outsz) {
  size_t o = 0;
  size_t i = 0;
  while (i < n) {
    while (i < n && in[i] == '/') ++i;
    size_t j = i;
    while (j < n && in[j] != '/') ++j;
    const size_t seg = j - i;
    if (seg == 0 || (seg == 1 && in[i] == '.')) {
      // skip
    } else if (seg == 2 && in[i] == '.' && in[i + 1] == '.') {
      if (o == 0) return false;
      while (o > 0 && out[o - 1] != '/') --o;
      if (o > 0) --o;  // the '/'
    } else {
      if (o + (o ? 1 : 0) + seg + 1 > outsz) return false;
      if (o) out[o++] = '/';
      memcpy(out + o, in + i, seg);
      o += seg;
    }
    i = j;
  }
  out[o] = '\0';
  return true;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t pctDecode(const char* in, size_t n, char* out, size_t outsz) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 1 < outsz; ++i) {
    if (in[i] == '%' && i + 2 < n && hexVal(in[i + 1]) >= 0 && hexVal(in[i + 2]) >= 0) {
      out[o++] = static_cast<char>(hexVal(in[i + 1]) << 4 | hexVal(in[i + 2]));
      i += 2;
    } else {
      out[o++] = in[i];
    }
  }
  out[o] = '\0';
  return o;
}

// Encodes what would end or break a markdown link target.
void pctEncode(const char* in, char* out, size_t outsz) {
  static const char* kHex = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < outsz; ++in) {
    const unsigned char c = static_cast<unsigned char>(*in);
    if (c == ' ' || c == '(' || c == ')' || c == '<' || c == '>' || c == '%' || c == '#' || c < 0x20) {
      out[o++] = '%';
      out[o++] = kHex[c >> 4];
      out[o++] = kHex[c & 15];
    } else {
      out[o++] = static_cast<char>(c);
    }
  }
  out[o] = '\0';
}

struct Ctx {
  const Rename& r;
  char oldFile[kPath];  // "old.md"
  char newFile[kPath];
  char oldStem[kPath];  // "old"
  char newStem[kPath];
  char noteDir[kPath];  // folder of the linking note ("" = root)
  int changed = 0;

  explicit Ctx(const Rename& rn, const char* notePath) : r(rn) {
    auto base = [](const char* p) {
      const char* s = strrchr(p, '/');
      return s ? s + 1 : p;
    };
    strncpy(oldFile, base(r.oldPath), kPath - 1);
    oldFile[kPath - 1] = '\0';
    strncpy(newFile, base(r.newPath), kPath - 1);
    newFile[kPath - 1] = '\0';
    strcpy(oldStem, oldFile);
    strcpy(newStem, newFile);
    if (endsWith(oldStem, strlen(oldStem), ".md")) oldStem[strlen(oldStem) - 3] = '\0';
    if (endsWith(newStem, strlen(newStem), ".md")) newStem[strlen(newStem) - 3] = '\0';
    const char* slash = strrchr(notePath, '/');
    const size_t d = slash ? static_cast<size_t>(slash - notePath) : 0;
    memcpy(noteDir, notePath, d);
    noteDir[d] = '\0';
  }

  // spec (normalized, root-relative) names the renamed note?
  bool isOld(const char* spec) const {
    const size_t n = strlen(spec);
    const size_t m = strlen(r.oldPath);
    if (n == m && memcmp(spec, r.oldPath, n) == 0) return true;
    return n + 3 == m && memcmp(spec, r.oldPath, n) == 0 && endsWith(r.oldPath, m, ".md");
  }

  // The renamed segment for a decoded old one ("old" or "old.md").
  const char* newSegmentFor(const char* decodedSeg) const {
    return strcmp(decodedSeg, oldFile) == 0 ? newFile : newStem;
  }
};

// Markdown target [t, t+n) (raw, without <>): writes the rewritten target to
// `out` and returns true if it points at the renamed note.
bool rewriteTarget(Ctx& c, const char* t, size_t n, bool angle, Out& out) {
  // Keep a "#fragment" as is.
  size_t pathLen = 0;
  while (pathLen < n && t[pathLen] != '#') ++pathLen;
  if (pathLen == 0) return false;
  for (size_t i = 0; i + 2 < pathLen; ++i) {
    if (t[i] == ':' && t[i + 1] == '/' && t[i + 2] == '/') return false;  // a URL
  }
  if (pathLen >= 7 && memcmp(t, "mailto:", 7) == 0) return false;

  char decoded[kPath];
  const size_t dn = pctDecode(t, pathLen, decoded, sizeof(decoded));
  char joined[kPath * 2];
  size_t jn;
  if (decoded[0] == '/') {
    jn = dn;
    memcpy(joined, decoded, dn);
  } else {
    jn = 0;
    const size_t dl = strlen(c.noteDir);
    memcpy(joined, c.noteDir, dl);
    jn = dl;
    if (dl) joined[jn++] = '/';
    memcpy(joined + jn, decoded, dn);
    jn += dn;
  }
  char spec[kPath];
  if (!normalize(joined, jn, spec, sizeof(spec)) || !c.isOld(spec)) return false;

  // Replace only the last segment, encoded the way the old one was.
  size_t segStart = pathLen;
  while (segStart > 0 && t[segStart - 1] != '/') --segStart;
  char seg[kPath];
  pctDecode(t + segStart, pathLen - segStart, seg, sizeof(seg));
  const char* repl = c.newSegmentFor(seg);
  const bool wasEncoded = memchr(t + segStart, '%', pathLen - segStart) != nullptr;
  char enc[kPath * 3];
  if (wasEncoded || (!angle && strpbrk(repl, " ()<>"))) {
    pctEncode(repl, enc, sizeof(enc));
    repl = enc;
  }
  out.put(t, segStart);
  out.put(repl, strlen(repl));
  out.put(t + pathLen, n - pathLen);
  ++c.changed;
  return true;
}

// Wiki link body [b, b+n) (between [[ and ]]).
void rewriteWiki(Ctx& c, const char* b, size_t n, Out& out) {
  size_t termEnd = 0;
  while (termEnd < n && b[termEnd] != '|') ++termEnd;
  size_t s = 0;
  while (s < termEnd && b[s] == ' ') ++s;
  size_t e = termEnd;
  while (e > s && b[e - 1] == ' ') --e;
  size_t baseEnd = s;
  while (baseEnd < e && b[baseEnd] != '#') ++baseEnd;
  const size_t baseLen = baseEnd - s;
  if (baseLen == 0 || baseLen >= kPath) {
    out.put(b, n);
    return;
  }
  char base[kPath];
  memcpy(base, b + s, baseLen);
  base[baseLen] = '\0';

  const char* repl = nullptr;
  size_t keep = 0;  // chars of base kept before the replacement
  if (strchr(base, '/')) {
    char spec[kPath];
    if (normalize(base, baseLen, spec, sizeof(spec)) && c.isOld(spec)) {
      const char* slash = strrchr(base, '/');
      keep = static_cast<size_t>(slash - base) + 1;
      repl = c.newSegmentFor(base + keep);
    }
  } else if (c.r.bareNameUnique && (strcmp(base, c.oldStem) == 0 || strcmp(base, c.oldFile) == 0)) {
    repl = c.newSegmentFor(base);
  }
  if (!repl) {
    out.put(b, n);
    return;
  }
  out.put(b, s + keep);
  out.put(repl, strlen(repl));
  out.put(b + baseEnd, n - baseEnd);
  ++c.changed;
}

// Rewrites one line (no '\n') outside code blocks.
void rewriteLine(Ctx& c, const char* l, size_t n, Out& out) {
  size_t i = 0;
  // Reference definition: "[label]: target" (up to 3 spaces of indent).
  size_t ind = 0;
  while (ind < n && ind < 3 && l[ind] == ' ') ++ind;
  if (ind < n && l[ind] == '[' && (ind + 1 >= n || l[ind + 1] != '[')) {
    const char* close = static_cast<const char*>(memchr(l + ind, ']', n - ind));
    if (close && static_cast<size_t>(close - l) + 1 < n && close[1] == ':') {
      size_t t = static_cast<size_t>(close - l) + 2;
      while (t < n && l[t] == ' ') ++t;
      const bool angle = t < n && l[t] == '<';
      const size_t ts = t + (angle ? 1 : 0);
      size_t te = ts;
      while (te < n && (angle ? l[te] != '>' : l[te] != ' ')) ++te;
      if (te > ts) {
        out.put(l, ts);
        if (!rewriteTarget(c, l + ts, te - ts, angle, out)) out.put(l + ts, te - ts);
        out.put(l + te, n - te);
        return;
      }
    }
  }

  while (i < n) {
    const char ch = l[i];
    if (ch == '`') {
      // Inline code: copy through the matching backtick run.
      size_t run = 0;
      while (i + run < n && l[i + run] == '`') ++run;
      size_t j = i + run;
      size_t endAt = 0;
      while (j < n) {
        if (l[j] == '`') {
          size_t r = 0;
          while (j + r < n && l[j + r] == '`') ++r;
          if (r == run) {
            endAt = j + r;
            break;
          }
          j += r;
        } else {
          ++j;
        }
      }
      const size_t upto = endAt ? endAt : i + run;
      out.put(l + i, upto - i);
      i = upto;
      continue;
    }
    if (ch == '[' && i + 1 < n && l[i + 1] == '[') {
      // [[wiki]]: no brackets inside (GitJournal's pattern).
      size_t j = i + 2;
      while (j < n && l[j] != '[' && l[j] != ']') ++j;
      if (j + 1 < n && l[j] == ']' && l[j + 1] == ']' && j > i + 2) {
        out.put("[[", 2);
        rewriteWiki(c, l + i + 2, j - i - 2, out);
        out.put("]]", 2);
        i = j + 2;
        continue;
      }
    }
    if (ch == '[') {
      // [text](target "title")
      const char* close = static_cast<const char*>(memchr(l + i + 1, ']', n - i - 1));
      if (close && static_cast<size_t>(close - l) + 1 < n && close[1] == '(') {
        size_t t = static_cast<size_t>(close - l) + 2;
        size_t ws = t;
        while (ws < n && l[ws] == ' ') ++ws;
        const bool angle = ws < n && l[ws] == '<';
        const size_t ts = ws + (angle ? 1 : 0);
        size_t te = ts;
        int depth = 0;
        while (te < n) {
          const char d = l[te];
          if (angle ? d == '>' : (d == ' ' || (d == ')' && depth == 0))) break;
          if (!angle && d == '(') ++depth;
          if (!angle && d == ')') --depth;
          ++te;
        }
        if (te < n && te > ts) {
          out.put(l + i, ts - i);
          if (!rewriteTarget(c, l + ts, te - ts, angle, out)) out.put(l + ts, te - ts);
          i = te;
          continue;
        }
      }
    }
    out.put(&ch, 1);
    ++i;
  }
}

bool isFence(const char* l, size_t n) {
  size_t i = 0;
  while (i < n && i < 3 && l[i] == ' ') ++i;
  return i + 3 <= n && ((l[i] == '`' && l[i + 1] == '`' && l[i + 2] == '`') ||
                        (l[i] == '~' && l[i + 1] == '~' && l[i + 2] == '~'));
}

}  // namespace

int rewrite(const char* text, size_t len, const char* notePath, const Rename& r, char* outBuf, size_t outCap,
            size_t& outLen) {
  Ctx c(r, notePath);
  Out out{outBuf, outCap};
  size_t p = 0;

  // Front matter is copied as is.
  if (len >= 4 && memcmp(text, "---\n", 4) == 0) {
    size_t q = 4;
    while (q < len) {
      const char* nl = static_cast<const char*>(memchr(text + q, '\n', len - q));
      const size_t e = nl ? static_cast<size_t>(nl - text) : len;
      if (e - q == 3 && memcmp(text + q, "---", 3) == 0) {
        p = nl ? e + 1 : len;
        break;
      }
      q = nl ? e + 1 : len;
    }
    out.put(text, p);
  }

  bool inFence = false;
  while (p < len) {
    const char* nl = static_cast<const char*>(memchr(text + p, '\n', len - p));
    const size_t e = nl ? static_cast<size_t>(nl - text) : len;
    const size_t n = e - p;
    if (isFence(text + p, n)) {
      inFence = !inFence;
      out.put(text + p, n);
    } else if (inFence) {
      out.put(text + p, n);
    } else {
      rewriteLine(c, text + p, n, out);
    }
    if (nl) out.put("\n", 1);
    p = nl ? e + 1 : len;
  }
  outLen = out.len;
  return out.over ? -1 : c.changed;
}

}  // namespace linkrewrite
