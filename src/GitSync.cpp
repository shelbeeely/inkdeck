#include "GitSync.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <SDCardManager.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "SyncUtil.h"
#include "TreeListing.h"

// Mozilla CA bundle compiled into the framework's mbedTLS (esp_crt_bundle).
extern "C" const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern "C" const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace GitSync {

namespace {

constexpr const char* kHost = "api.github.com";
constexpr size_t kMaxPath = TreeListing::kMaxPath;  // repo-relative path
constexpr size_t kMaxLocalPath = 232;  // notes folder + '/' + repo path
constexpr uint16_t kMaxBase = 1024;    // files tracked in the state file
constexpr uint16_t kMaxDone = 2048;    // paths handled in one run
constexpr uint8_t kMaxNetFailures = 3;

struct BaseEntry {
  uint32_t hash;
  uint8_t sha[20];
};

// --- Run state (allocated for the duration of run()) ------------------------
struct Ctx {
  NetworkClientSecure tls;
  HTTPClient http;
  Progress progress = nullptr;
  Result result;
  BaseEntry* base = nullptr;
  uint16_t baseCount = 0;
  uint32_t* done = nullptr;
  uint16_t doneCount = 0;
  uint8_t netFailures = 0;
  bool baseMatchesRepo = false;  // the state file was written for this repo/branch
  FsFile newState;
  char repoEsc[128];
  char branchEsc[96];
  char branchJson[96];
};
Ctx* g = nullptr;

void say(const char* fmt, const char* arg = nullptr) {
  if (!g->progress) return;
  char line[96];
  snprintf(line, sizeof(line), fmt, arg ? arg : "");
  g->progress(line);
}

void fail(const char* msg) {
  strncpy(g->result.error, msg, sizeof(g->result.error) - 1);
  g->result.error[sizeof(g->result.error) - 1] = '\0';
}

void localPathFor(const char* repoPath, char* out, size_t outsz) {
  snprintf(out, outsz, "%s/%s", g_config.notesFolder, repoPath);
}

void statePath(char* out, size_t outsz, const char* suffix) {
  snprintf(out, outsz, "%s/.inkdeck-%s", g_config.notesFolder, suffix);
}

bool isDone(uint32_t h) {
  for (uint16_t i = 0; i < g->doneCount; ++i) {
    if (g->done[i] == h) return true;
  }
  return false;
}

void markDone(const char* path) {
  if (g->doneCount < kMaxDone) g->done[g->doneCount++] = syncutil::pathHash(path);
}

bool baseFor(const char* path, char hex[41]) {
  const uint32_t h = syncutil::pathHash(path);
  for (uint16_t i = 0; i < g->baseCount; ++i) {
    if (g->base[i].hash == h) {
      syncutil::toHex(g->base[i].sha, hex);
      return true;
    }
  }
  return false;
}

void recordState(const char* path, const char* hex) {
  if (!g->newState) return;
  g->newState.write(reinterpret_cast<const uint8_t*>(hex), 40);
  g->newState.write(' ');
  g->newState.write(reinterpret_cast<const uint8_t*>(path), strlen(path));
  g->newState.write('\n');
}

// Splits a "<40 hex> <path>" line. Returns false for malformed and header lines.
bool splitLine(char* line, char** sha, char** path) {
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
  if (line[0] == '#' || n < 42 || line[40] != ' ') return false;
  line[40] = '\0';
  *sha = line;
  *path = line + 41;
  return true;
}

// First line of the state file. Sync history only applies to the repo and
// branch it was recorded against: pointing the notes folder at another repo
// must look like a first sync (keep everything), not like mass deletions.
void repoHeader(char* out, size_t outsz) {
  snprintf(out, outsz, "# %s %s\n", g_config.githubRepo, g_config.githubBranch);
}

bool loadBase() {
  char p[96];
  statePath(p, sizeof(p), "sync");
  FsFile f = SdMan.open(p, O_RDONLY);
  if (!f) return true;  // first sync
  char line[48 + kMaxPath];
  char header[200];
  repoHeader(header, sizeof(header));
  if (f.fgets(line, sizeof(line)) <= 0 || strcmp(line, header) != 0) {
    f.close();
    say("New repo or branch: merging, not deleting");
    return true;
  }
  g->baseMatchesRepo = true;
  while (f.available()) {
    if (f.fgets(line, sizeof(line)) <= 0) break;
    char *sha, *path;
    if (!splitLine(line, &sha, &path)) continue;
    if (g->baseCount >= kMaxBase) {
      f.close();
      fail("Too many notes to sync");
      return false;
    }
    BaseEntry& e = g->base[g->baseCount];
    if (!syncutil::fromHex(sha, e.sha)) continue;
    e.hash = syncutil::pathHash(path);
    ++g->baseCount;
  }
  f.close();
  return true;
}

// Git blob id of a local file; false if it doesn't exist.
bool localSha(const char* localPath, char hex[41]) {
  FsFile f = SdMan.open(localPath, O_RDONLY);
  if (!f || f.isDirectory()) return false;
  syncutil::Sha1 s;
  syncutil::blobShaBegin(s, static_cast<uint32_t>(f.size()));
  uint8_t buf[512];
  int r;
  while ((r = f.read(buf, sizeof(buf))) > 0) syncutil::sha1Update(s, buf, static_cast<size_t>(r));
  f.close();
  uint8_t d[20];
  syncutil::sha1Final(s, d);
  syncutil::toHex(d, hex);
  return true;
}

void ensureParentDir(const char* localPath) {
  char dir[kMaxLocalPath];
  strncpy(dir, localPath, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';
  char* slash = strrchr(dir, '/');
  if (!slash || slash == dir) return;
  *slash = '\0';
  if (!SdMan.exists(dir)) SdMan.mkdir(dir);
}

// --- HTTP ---------------------------------------------------------------------
// Discards a response body so the keep-alive connection stays usable.
class NullSink : public Stream {
 public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t n) override { return n; }
};

bool beginRequest(const char* uri, const char* accept = "application/vnd.github+json") {
  if (!g->http.begin(g->tls, kHost, 443, uri, true)) return false;
  g->http.setReuse(true);
  g->http.setTimeout(20000);
  g->http.setConnectTimeout(15000);
  g->http.setUserAgent("InkDeck");
  char auth[176];
  snprintf(auth, sizeof(auth), "Bearer %s", g_config.githubToken);
  g->http.addHeader("Authorization", auth);
  g->http.addHeader("Accept", accept);
  g->http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  return true;
}

void finishRequest(int code) {
  if (code > 0) {
    NullSink sink;
    g->http.writeToStream(&sink);
  }
  g->http.end();
}

// Counts transport failures (no HTTP status) so a dropped network aborts the
// run instead of failing every file one by one.
bool noteStatus(int code) {
  if (code > 0) {
    g->netFailures = 0;
    return true;
  }
  ++g->netFailures;
  return false;
}

// --- Tree listing ---------------------------------------------------------------
void writeTreeLine(void* ctx, const char* sha, const char* path) {
  auto* out = static_cast<FsFile*>(ctx);
  out->write(reinterpret_cast<const uint8_t*>(sha), 40);
  out->write(' ');
  out->write(reinterpret_cast<const uint8_t*>(path), strlen(path));
  out->write('\n');
}

// Feeds a response body straight into the tree listing parser.
class TreeSink : public Stream {
 public:
  explicit TreeSink(TreeListing& t) : t_(t) {}
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t c) override {
    t_.feed(reinterpret_cast<const char*>(&c), 1);
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    t_.feed(reinterpret_cast<const char*>(b), n);
    return n;
  }

 private:
  TreeListing& t_;
};

bool checkRepo() {
  char uri[160];
  snprintf(uri, sizeof(uri), "/repos/%s", g->repoEsc);
  if (!beginRequest(uri)) {
    fail("Bad repository name");
    return false;
  }
  const int code = g->http.GET();
  finishRequest(code);
  if (code == 200) return true;
  if (code == 401) fail("GitHub token rejected");
  else if (code == 403) fail("Token lacks access (or rate limited)");
  else if (code == 404) fail("Repo not found for this token");
  else if (code < 0) fail("Can't reach GitHub");
  else {
    char msg[40];
    snprintf(msg, sizeof(msg), "GitHub error %d", code);
    fail(msg);
  }
  return false;
}

// Returns false on error; an empty repository yields an empty list.
bool fetchTree(const char* treePath, uint16_t& count) {
  FsFile out = SdMan.open(treePath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) {
    fail("Can't write to SD card");
    return false;
  }
  char uri[256];
  snprintf(uri, sizeof(uri), "/repos/%s/git/trees/%s?recursive=1", g->repoEsc, g->branchEsc);
  if (!beginRequest(uri)) {
    out.close();
    fail("Bad branch name");
    return false;
  }
  const int code = g->http.GET();
  bool ok = false;
  if (code == 200) {
    // The parser carries a 2 KB token buffer; keep it off the stack.
    auto* listing = new TreeListing(writeTreeLine, &out);
    TreeSink sink(*listing);
    const int n = g->http.writeToStream(&sink);
    ok = n >= 0 && !listing->hasError();
    count = listing->count();
    if (!ok) {
      fail("Couldn't read file list");
    } else if (listing->truncated()) {
      // A partial listing would make missing files look deleted.
      ok = false;
      fail("Repo too large to list");
    }
    delete listing;
    g->http.end();
  } else {
    finishRequest(code);
    if (code == 409) {  // "Git Repository is empty."
      ok = true;
      count = 0;
    } else if (code == 404) {
      char msg[64];
      snprintf(msg, sizeof(msg), "Branch '%.40s' not found", g_config.githubBranch);
      fail(msg);
    } else {
      char msg[40];
      snprintf(msg, sizeof(msg), "File list failed (%d)", code);
      fail(msg);
    }
  }
  out.close();
  noteStatus(code);
  return ok;
}

// --- File operations ----------------------------------------------------------
bool contentsUri(const char* repoPath, char* uri, size_t urisz, bool withRef) {
  char esc[kMaxPath * 3];
  if (!syncutil::urlEscapePath(repoPath, esc, sizeof(esc))) return false;
  const int n = withRef ? snprintf(uri, urisz, "/repos/%s/contents/%s?ref=%s", g->repoEsc, esc, g->branchEsc)
                        : snprintf(uri, urisz, "/repos/%s/contents/%s", g->repoEsc, esc);
  return n > 0 && static_cast<size_t>(n) < urisz;
}

// Downloads the remote file into place (via a temp file). On success `hex`
// holds the blob id of what was written.
bool pull(const char* repoPath, char hex[41]) {
  char uri[640];
  if (!contentsUri(repoPath, uri, sizeof(uri), true) || !beginRequest(uri, "application/vnd.github.raw+json")) {
    return false;
  }
  char local[kMaxLocalPath];
  localPathFor(repoPath, local, sizeof(local));
  char tmp[kMaxLocalPath + 8];
  snprintf(tmp, sizeof(tmp), "%s.dltmp", local);
  ensureParentDir(local);

  const int code = g->http.GET();
  noteStatus(code);
  if (code != 200) {
    finishRequest(code);
    return false;
  }
  FsFile f = SdMan.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) {
    finishRequest(code);
    return false;
  }
  const int n = g->http.writeToStream(&f);
  f.close();
  g->http.end();
  if (n < 0 || !SdMan.replaceFile(tmp, local)) {
    SdMan.remove(tmp);
    return false;
  }
  return localSha(local, hex);
}

// Request body for the contents PUT: {"message":..,"branch":..,["sha":..,]
// "content":"<base64 of the file>"}, produced on the fly so the file is never
// held in RAM.
class PutBody : public Stream {
 public:
  PutBody(FsFile& f, const char* prefix) : f_(f) {
    strncpy(prefix_, prefix, sizeof(prefix_) - 1);
    prefix_[sizeof(prefix_) - 1] = '\0';
    prefixLen_ = strlen(prefix_);
    total_ = prefixLen_ + syncutil::base64Len(static_cast<uint32_t>(f.size())) + 2;
  }
  size_t size() const { return total_; }
  int available() override { return static_cast<int>(total_ - pos_); }
  int read() override {
    uint8_t c;
    return readBytes(reinterpret_cast<char*>(&c), 1) == 1 ? c : -1;
  }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 0; }
  size_t readBytes(char* out, size_t n) override {
    size_t o = 0;
    while (o < n && pos_ < total_) {
      if (pos_ < prefixLen_) {
        out[o++] = prefix_[pos_++];
      } else if (pos_ < total_ - 2) {
        if (chunkPos_ >= chunkLen_) refill();
        if (chunkLen_ == 0) break;  // file shorter than expected
        out[o++] = chunk_[chunkPos_++];
        ++pos_;
      } else {
        out[o++] = (pos_ == total_ - 2) ? '"' : '}';
        ++pos_;
      }
    }
    return o;
  }

 private:
  void refill() {
    uint8_t raw[384];  // multiple of 3 so only the last chunk pads
    const int r = f_.read(raw, sizeof(raw));
    chunkLen_ = r > 0 ? syncutil::base64Encode(raw, static_cast<size_t>(r), chunk_) : 0;
    chunkPos_ = 0;
  }

  FsFile& f_;
  char prefix_[448];
  size_t prefixLen_ = 0;
  size_t total_ = 0;
  size_t pos_ = 0;
  char chunk_[512];
  size_t chunkLen_ = 0;
  size_t chunkPos_ = 0;
};

bool push(const char* repoPath, const char* localPath, const char* remoteSha) {
  char uri[640];
  if (!contentsUri(repoPath, uri, sizeof(uri), false) || !beginRequest(uri)) return false;
  FsFile f = SdMan.open(localPath, O_RDONLY);
  if (!f) {
    g->http.end();
    return false;
  }
  char pathJson[kMaxPath * 2];
  syncutil::jsonEscape(repoPath, pathJson, sizeof(pathJson));
  char prefix[448];
  if (remoteSha) {
    snprintf(prefix, sizeof(prefix), "{\"message\":\"InkDeck: update %s\",\"branch\":\"%s\",\"sha\":\"%s\",\"content\":\"",
             pathJson, g->branchJson, remoteSha);
  } else {
    snprintf(prefix, sizeof(prefix), "{\"message\":\"InkDeck: add %s\",\"branch\":\"%s\",\"content\":\"", pathJson,
             g->branchJson);
  }
  auto* body = new PutBody(f, prefix);
  g->http.addHeader("Content-Type", "application/json");
  const int code = g->http.sendRequest("PUT", body, body->size());
  delete body;
  f.close();
  noteStatus(code);
  finishRequest(code);
  return code == 200 || code == 201;
}

bool removeRemote(const char* repoPath, const char* remoteSha) {
  char uri[640];
  if (!contentsUri(repoPath, uri, sizeof(uri), false) || !beginRequest(uri)) return false;
  char pathJson[kMaxPath * 2];
  syncutil::jsonEscape(repoPath, pathJson, sizeof(pathJson));
  char body[448];
  snprintf(body, sizeof(body), "{\"message\":\"InkDeck: delete %s\",\"branch\":\"%s\",\"sha\":\"%s\"}", pathJson,
           g->branchJson, remoteSha);
  g->http.addHeader("Content-Type", "application/json");
  const int code = g->http.sendRequest("DELETE", String(body));
  noteStatus(code);
  finishRequest(code);
  return code == 200 || code == 404;  // already gone counts as done
}

// "<stem> (InkDeck conflict).md", numbered if that exists locally too.
void conflictPath(const char* repoPath, char* out, size_t outsz) {
  char stem[kMaxPath];
  strncpy(stem, repoPath, sizeof(stem) - 1);
  stem[sizeof(stem) - 1] = '\0';
  stem[strlen(stem) - 3] = '\0';  // drop ".md"
  for (int i = 1; i < 100; ++i) {
    if (i == 1) snprintf(out, outsz, "%s (InkDeck conflict).md", stem);
    else snprintf(out, outsz, "%s (InkDeck conflict %d).md", stem, i);
    char local[kMaxLocalPath];
    localPathFor(out, local, sizeof(local));
    if (!SdMan.exists(local)) return;
  }
}

// --- Per-path reconciliation ---------------------------------------------------
void reconcile(const char* path, const char* remote) {
  markDone(path);
  char local[kMaxLocalPath];
  localPathFor(path, local, sizeof(local));
  char localHex[41], baseHex[41];
  const bool hasLocal = localSha(local, localHex);
  const bool hasBase = baseFor(path, baseHex);
  const char* l = hasLocal ? localHex : nullptr;
  const char* b = hasBase ? baseHex : nullptr;

  bool ok = true;
  switch (syncutil::decide(l, remote, b)) {
    case syncutil::Action::None:
      if (l) recordState(path, l);
      return;
    case syncutil::Action::Pull: {
      say("Down: %s", path);
      char got[41];
      ok = pull(path, got);
      if (ok) {
        recordState(path, got);
        ++g->result.pulled;
      }
      break;
    }
    case syncutil::Action::Push:
      say("Up: %s", path);
      ok = push(path, local, remote);
      if (ok) {
        recordState(path, l);
        ++g->result.pushed;
      }
      break;
    case syncutil::Action::DeleteLocal:
      say("Delete here: %s", path);
      ok = SdMan.remove(local);
      if (ok) ++g->result.deleted;
      break;
    case syncutil::Action::DeleteRemote:
      say("Delete on GitHub: %s", path);
      ok = removeRemote(path, remote);
      if (ok) ++g->result.deleted;
      break;
    case syncutil::Action::Conflict: {
      say("Conflict: %s", path);
      char copy[kMaxPath + 32];
      conflictPath(path, copy, sizeof(copy));
      if (strlen(copy) >= kMaxPath) {  // too long to track: leave both sides alone
        ok = false;
        break;
      }
      char copyLocal[kMaxLocalPath];
      localPathFor(copy, copyLocal, sizeof(copyLocal));
      // Keep the device's text as the conflict copy (here and on GitHub), then
      // take the remote version under the original name.
      ok = SdMan.rename(local, copyLocal);
      if (ok) {
        markDone(copy);
        if (push(copy, copyLocal, nullptr)) recordState(copy, l);
        else ++g->result.failed;
        char got[41];
        ok = pull(path, got);
        if (ok) recordState(path, got);
      }
      if (ok) ++g->result.conflicts;
      break;
    }
  }
  if (!ok) {
    ++g->result.failed;
    if (b) recordState(path, b);  // keep the old base so the next run retries cleanly
  }
}

// Recursively visits local notes the remote listing didn't cover.
void walkLocal(const char* dirLocal, const char* dirRepo) {
  if (g->netFailures >= kMaxNetFailures) return;
  FsFile dir = SdMan.open(dirLocal, O_RDONLY);
  if (!dir || !dir.isDirectory()) return;
  char name[96];
  for (FsFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const bool isDir = f.isDirectory();
    f.getName(name, sizeof(name));
    f.close();
    if (name[0] == '.') continue;
    char repoPath[kMaxPath];
    const int n = dirRepo[0] ? snprintf(repoPath, sizeof(repoPath), "%s/%s", dirRepo, name)
                             : snprintf(repoPath, sizeof(repoPath), "%s", name);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(repoPath)) continue;
    if (isDir) {
      char sub[kMaxLocalPath];
      snprintf(sub, sizeof(sub), "%s/%s", dirLocal, name);
      walkLocal(sub, repoPath);
    } else if (syncutil::isSyncedNotePath(repoPath) && !isDone(syncutil::pathHash(repoPath))) {
      reconcile(repoPath, nullptr);
    }
    if (g->netFailures >= kMaxNetFailures) break;
  }
  dir.close();
}

// After an aborted run, carry over base entries for paths not reached so the
// next run still knows their history.
void carryOverBase() {
  if (!g->baseMatchesRepo) return;
  char p[96];
  statePath(p, sizeof(p), "sync");
  FsFile f = SdMan.open(p, O_RDONLY);
  if (!f) return;
  char line[48 + kMaxPath];
  while (f.available()) {
    if (f.fgets(line, sizeof(line)) <= 0) break;
    char *sha, *path;
    if (!splitLine(line, &sha, &path)) continue;
    if (!isDone(syncutil::pathHash(path))) recordState(path, sha);
  }
  f.close();
}

}  // namespace

Result run(Progress progress) {
  Ctx ctx;
  g = &ctx;
  ctx.progress = progress;
  ctx.tls.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);

  if (!syncutil::urlEscapePath(g_config.githubRepo, ctx.repoEsc, sizeof(ctx.repoEsc)) ||
      !syncutil::urlEscapePath(g_config.githubBranch, ctx.branchEsc, sizeof(ctx.branchEsc)) ||
      !syncutil::jsonEscape(g_config.githubBranch, ctx.branchJson, sizeof(ctx.branchJson))) {
    fail("Repo or branch name too long");
    g = nullptr;
    return ctx.result;
  }
  if (!SdMan.exists(g_config.notesFolder) && !SdMan.mkdir(g_config.notesFolder)) {
    fail("Can't create notes folder");
    g = nullptr;
    return ctx.result;
  }

  ctx.base = static_cast<BaseEntry*>(malloc(sizeof(BaseEntry) * kMaxBase));
  ctx.done = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * kMaxDone));
  if (!ctx.base || !ctx.done) {
    fail("Out of memory");
  } else if (loadBase()) {
    say("Connecting to GitHub...");
    char treePath[96], newPath[96], statePathBuf[96];
    statePath(treePath, sizeof(treePath), "tree");
    statePath(newPath, sizeof(newPath), "sync.new");
    statePath(statePathBuf, sizeof(statePathBuf), "sync");
    uint16_t remoteCount = 0;
    if (checkRepo() && fetchTree(treePath, remoteCount)) {
      char msg[48];
      snprintf(msg, sizeof(msg), "%u notes on GitHub", remoteCount);
      say("%s", msg);
      if (remoteCount > kMaxDone / 2) {
        fail("Too many notes to sync");
      } else if (!(ctx.newState = SdMan.open(newPath, O_WRONLY | O_CREAT | O_TRUNC))) {
        fail("Can't write to SD card");
      } else {
        char header[200];
        repoHeader(header, sizeof(header));
        ctx.newState.write(reinterpret_cast<const uint8_t*>(header), strlen(header));
        FsFile tree = SdMan.open(treePath, O_RDONLY);
        char line[48 + kMaxPath];
        while (tree && tree.available() && ctx.netFailures < kMaxNetFailures) {
          if (tree.fgets(line, sizeof(line)) <= 0) break;
          char *sha, *path;
          // A conflict copy made earlier in this run may also be listed.
          if (splitLine(line, &sha, &path) && !isDone(syncutil::pathHash(path))) reconcile(path, sha);
        }
        if (tree) tree.close();
        walkLocal(g_config.notesFolder, "");

        const bool aborted = ctx.netFailures >= kMaxNetFailures;
        if (aborted) {
          carryOverBase();
          fail("Network dropped; sync incomplete");
        }
        ctx.newState.close();
        if (!SdMan.replaceFile(newPath, statePathBuf)) fail("Can't save sync state");
        ctx.result.ok = !aborted && ctx.result.error[0] == '\0';
      }
      SdMan.remove(treePath);
    }
  }

  free(ctx.base);
  free(ctx.done);
  g = nullptr;
  return ctx.result;
}

}  // namespace GitSync
