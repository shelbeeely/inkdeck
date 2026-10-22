#include "TreeListing.h"

#include <string.h>

#include "SyncUtil.h"

// Shape: {"sha":..,"url":..,"tree":[{"path":..,"mode":..,"type":"blob","sha":..,
// "size":..,"url":..}, ...],"truncated":false}. Root object is depth 1, tree
// entries are depth 2.
TreeListing::TreeListing(Emit emit, void* ctx)
    : emit_(emit),
      ctx_(ctx),
      parser_(JsonCallbacks{this, onKey, onString, ignoreValue, onBool, ignore, onObjectStart, onObjectEnd,
                            onArrayStart, onArrayEnd}) {}

void TreeListing::onKey(void* c, const char* k, size_t n) {
  auto* t = static_cast<TreeListing*>(c);
  if (n < sizeof(t->key_)) {
    memcpy(t->key_, k, n);
    t->key_[n] = '\0';
  } else {
    t->key_[0] = '\0';  // a long key we don't care about
  }
  t->nextIsTree_ = t->depth_ == 1 && strcmp(t->key_, "tree") == 0;
}

void TreeListing::onString(void* c, const char* v, size_t n) {
  auto* t = static_cast<TreeListing*>(c);
  if (!t->inTree_ || t->depth_ != 2) return;
  if (strcmp(t->key_, "path") == 0) {
    t->pathTooLong_ = n >= sizeof(t->path_);
    const size_t m = t->pathTooLong_ ? 0 : n;
    memcpy(t->path_, v, m);
    t->path_[m] = '\0';
  } else if (strcmp(t->key_, "sha") == 0 && n == 40) {
    memcpy(t->sha_, v, 40);
    t->sha_[40] = '\0';
  } else if (strcmp(t->key_, "type") == 0) {
    t->blob_ = n == 4 && memcmp(v, "blob", 4) == 0;
  }
}

void TreeListing::onBool(void* c, bool v) {
  auto* t = static_cast<TreeListing*>(c);
  if (t->depth_ == 1 && strcmp(t->key_, "truncated") == 0) t->truncated_ = v;
}

void TreeListing::onObjectStart(void* c) {
  auto* t = static_cast<TreeListing*>(c);
  ++t->depth_;
  if (t->inTree_ && t->depth_ == 2) {
    t->path_[0] = t->sha_[0] = '\0';
    t->blob_ = t->pathTooLong_ = false;
  }
}

void TreeListing::onObjectEnd(void* c) {
  auto* t = static_cast<TreeListing*>(c);
  if (t->inTree_ && t->depth_ == 2 && t->blob_ && t->sha_[0] && t->path_[0] && !t->pathTooLong_ &&
      syncutil::isSyncedNotePath(t->path_)) {
    t->emit_(t->ctx_, t->sha_, t->path_);
    ++t->count_;
  }
  if (t->depth_ > 0) --t->depth_;
}

void TreeListing::onArrayStart(void* c) {
  auto* t = static_cast<TreeListing*>(c);
  if (t->nextIsTree_) t->inTree_ = true;
  t->nextIsTree_ = false;
}

void TreeListing::onArrayEnd(void* c) {
  auto* t = static_cast<TreeListing*>(c);
  if (t->depth_ == 1) t->inTree_ = false;
}
