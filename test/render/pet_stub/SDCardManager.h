#pragma once
// Reads files from the host's disk for the Pet screen render.
#include <cstdio>
#include <cstdint>
#define O_RDONLY 0
struct FsFile {
  FILE* fp = nullptr;
  explicit operator bool() const { return fp != nullptr; }
  int read(uint8_t* b, size_t n) { return fp ? static_cast<int>(fread(b, 1, n, fp)) : -1; }
  void close() { if (fp) fclose(fp); fp = nullptr; }
};
struct SdManT {
  FsFile open(const char* p, int) { FsFile f; f.fp = fopen(p, "rb"); return f; }
};
inline SdManT SdMan;
