#pragma once

// Streaming reader for GitHub's recursive tree listing
// (GET /repos/{owner}/{repo}/git/trees/{branch}?recursive=1). Feed the response
// body in chunks; each synced note blob ("*.md", no hidden segment) is reported
// as (sha, path). No Arduino dependencies, so it is host-testable.

#include <StreamingJsonParser.h>
#include <stddef.h>
#include <stdint.h>

class TreeListing {
 public:
  using Emit = void (*)(void* ctx, const char* sha, const char* path);
  static constexpr size_t kMaxPath = 160;

  TreeListing(Emit emit, void* ctx);
  void feed(const char* data, size_t len) { parser_.feed(data, len); }

  bool hasError() const { return parser_.hasError(); }
  bool truncated() const { return truncated_; }  // GitHub cut the listing short
  uint16_t count() const { return count_; }

 private:
  static void onKey(void* c, const char* k, size_t n);
  static void onString(void* c, const char* v, size_t n);
  static void onBool(void* c, bool v);
  static void onObjectStart(void* c);
  static void onObjectEnd(void* c);
  static void onArrayStart(void* c);
  static void onArrayEnd(void* c);
  static void ignoreValue(void*, const char*, size_t) {}
  static void ignore(void*) {}

  Emit emit_;
  void* ctx_;
  uint8_t depth_ = 0;
  bool inTree_ = false;
  bool nextIsTree_ = false;
  char key_[12] = {0};
  char path_[kMaxPath] = {0};
  char sha_[41] = {0};
  bool blob_ = false;
  bool pathTooLong_ = false;
  bool truncated_ = false;
  uint16_t count_ = 0;
  StreamingJsonParser parser_;
};
