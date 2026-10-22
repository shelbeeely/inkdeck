#include "SyncUtil.h"

#include <stdio.h>
#include <string.h>

namespace syncutil {

// ---------------------------------------------------------------------------
// SHA-1
// ---------------------------------------------------------------------------
namespace {
inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void sha1Block(Sha1& s, const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
  }
  for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3], e = s.h[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    const uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = t;
  }
  s.h[0] += a;
  s.h[1] += b;
  s.h[2] += c;
  s.h[3] += d;
  s.h[4] += e;
}
}  // namespace

void sha1Init(Sha1& s) {
  s.h[0] = 0x67452301;
  s.h[1] = 0xEFCDAB89;
  s.h[2] = 0x98BADCFE;
  s.h[3] = 0x10325476;
  s.h[4] = 0xC3D2E1F0;
  s.total = 0;
  s.used = 0;
}

void sha1Update(Sha1& s, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  s.total += len;
  while (len > 0) {
    const size_t take = (64 - s.used) < len ? (64 - s.used) : len;
    memcpy(s.block + s.used, p, take);
    s.used = static_cast<uint8_t>(s.used + take);
    p += take;
    len -= take;
    if (s.used == 64) {
      sha1Block(s, s.block);
      s.used = 0;
    }
  }
}

void sha1Final(Sha1& s, uint8_t out[20]) {
  const uint64_t bits = s.total * 8;
  const uint8_t pad = 0x80;
  sha1Update(s, &pad, 1);
  const uint8_t zero = 0;
  while (s.used != 56) sha1Update(s, &zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; ++i) len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
  sha1Update(s, len, 8);
  for (int i = 0; i < 5; ++i) {
    out[i * 4] = static_cast<uint8_t>(s.h[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(s.h[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(s.h[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(s.h[i]);
  }
}

void blobShaBegin(Sha1& s, uint32_t contentLen) {
  sha1Init(s);
  char hdr[24];
  const int n = snprintf(hdr, sizeof(hdr), "blob %lu", static_cast<unsigned long>(contentLen));
  sha1Update(s, hdr, static_cast<size_t>(n) + 1);  // include the NUL
}

void toHex(const uint8_t sha[20], char out[41]) {
  static const char* kHex = "0123456789abcdef";
  for (int i = 0; i < 20; ++i) {
    out[i * 2] = kHex[sha[i] >> 4];
    out[i * 2 + 1] = kHex[sha[i] & 0xF];
  }
  out[40] = '\0';
}

bool fromHex(const char* hex, uint8_t out[20]) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (int i = 0; i < 20; ++i) {
    const int hi = nib(hex[i * 2]);
    const int lo = hi < 0 ? -1 : nib(hex[i * 2 + 1]);
    if (lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Base64
// ---------------------------------------------------------------------------
size_t base64Encode(const uint8_t* in, size_t n, char* out) {
  static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    const uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
    out[o++] = kB64[(v >> 18) & 63];
    out[o++] = kB64[(v >> 12) & 63];
    out[o++] = kB64[(v >> 6) & 63];
    out[o++] = kB64[v & 63];
  }
  if (i < n) {
    uint32_t v = uint32_t(in[i]) << 16;
    if (i + 1 < n) v |= uint32_t(in[i + 1]) << 8;
    out[o++] = kB64[(v >> 18) & 63];
    out[o++] = kB64[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? kB64[(v >> 6) & 63] : '=';
    out[o++] = '=';
  }
  return o;
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------
bool urlEscapePath(const char* in, char* out, size_t outsz) {
  static const char* kHex = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
    const unsigned char c = *p;
    const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                       c == '_' || c == '.' || c == '~' || c == '/';
    if (plain) {
      if (o + 1 >= outsz) return false;
      out[o++] = static_cast<char>(c);
    } else {
      if (o + 3 >= outsz) return false;
      out[o++] = '%';
      out[o++] = kHex[c >> 4];
      out[o++] = kHex[c & 0xF];
    }
  }
  out[o] = '\0';
  return true;
}

bool jsonEscape(const char* in, char* out, size_t outsz) {
  size_t o = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p; ++p) {
    const unsigned char c = *p;
    char esc[8];
    const char* s = nullptr;
    if (c == '"') s = "\\\"";
    else if (c == '\\') s = "\\\\";
    else if (c == '\n') s = "\\n";
    else if (c == '\t') s = "\\t";
    else if (c < 0x20) {
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      s = esc;
    }
    if (s) {
      const size_t n = strlen(s);
      if (o + n >= outsz) return false;
      memcpy(out + o, s, n);
      o += n;
    } else {
      if (o + 1 >= outsz) return false;
      out[o++] = static_cast<char>(c);
    }
  }
  out[o] = '\0';
  return true;
}

uint32_t pathHash(const char* path) {
  uint32_t h = 2166136261u;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(path); *p; ++p) {
    h ^= *p;
    h *= 16777619u;
  }
  return h;
}

bool isSyncedNotePath(const char* path) {
  const size_t n = strlen(path);
  if (n < 4 || strcmp(path + n - 3, ".md") != 0) return false;
  if (path[0] == '.') return false;
  return strstr(path, "/.") == nullptr;
}

// ---------------------------------------------------------------------------
// Decision table
// ---------------------------------------------------------------------------
Action decide(const char* local, const char* remote, const char* base) {
  auto same = [](const char* a, const char* b) { return a && b && strcmp(a, b) == 0; };
  if (local && remote) {
    if (same(local, remote)) return Action::None;
    if (same(local, base)) return Action::Pull;
    if (same(remote, base)) return Action::Push;
    return Action::Conflict;
  }
  if (remote) {  // no local file
    if (same(remote, base)) return Action::DeleteRemote;
    return Action::Pull;  // new remotely, or edited remotely after a local delete
  }
  if (local) {  // no remote file
    if (same(local, base)) return Action::DeleteLocal;
    return Action::Push;  // new locally, or edited locally after a remote delete
  }
  return Action::None;
}

}  // namespace syncutil
