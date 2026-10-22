#pragma once

// Pure helpers for GitJournal sync (no Arduino/SD dependencies, so they can be
// compiled and checked on a host): git blob hashing, base64, URL escaping,
// path hashing, and the per-file sync decision.

#include <stddef.h>
#include <stdint.h>

namespace syncutil {

// --- SHA-1 (git object ids) --------------------------------------------------
struct Sha1 {
  uint32_t h[5];
  uint64_t total;
  uint8_t block[64];
  uint8_t used;
};
void sha1Init(Sha1& s);
void sha1Update(Sha1& s, const void* data, size_t len);
void sha1Final(Sha1& s, uint8_t out[20]);

// Git blob id = sha1("blob <len>\0" + content). Start with the size, feed the
// content in any number of chunks, then finish into 40 lowercase hex chars.
void blobShaBegin(Sha1& s, uint32_t contentLen);
void toHex(const uint8_t sha[20], char out[41]);
bool fromHex(const char* hex, uint8_t out[20]);  // false unless 40 hex chars

// --- Base64 -------------------------------------------------------------------
inline uint32_t base64Len(uint32_t n) { return ((n + 2) / 3) * 4; }
// Encodes `n` bytes (n must be a multiple of 3 unless this is the final chunk).
// Returns the number of chars written to `out` (no NUL).
size_t base64Encode(const uint8_t* in, size_t n, char* out);

// --- Strings ------------------------------------------------------------------
// Percent-escapes a repo path for use in a URL (keeps '/' and unreserved chars).
// Returns false if it does not fit.
bool urlEscapePath(const char* in, char* out, size_t outsz);
// Escapes a string for a JSON string literal body (no surrounding quotes).
bool jsonEscape(const char* in, char* out, size_t outsz);
// FNV-1a 32-bit, used to key paths in fixed-size tables.
uint32_t pathHash(const char* path);
// True for the note files we sync: "*.md", no hidden path segment.
bool isSyncedNotePath(const char* path);

// --- Per-file decision ----------------------------------------------------------
// Inputs are 40-char hex ids or nullptr when absent: the local file, the remote
// file, and the id both sides had at the last successful sync.
enum class Action : uint8_t {
  None,          // already in sync (or nothing anywhere)
  Pull,          // remote changed / new remote file -> download
  Push,          // local changed / new local file -> upload
  DeleteLocal,   // removed remotely, unchanged locally
  DeleteRemote,  // removed locally, unchanged remotely
  Conflict,      // both changed: keep both (local is pushed as a copy)
};
Action decide(const char* local, const char* remote, const char* base);

}  // namespace syncutil
