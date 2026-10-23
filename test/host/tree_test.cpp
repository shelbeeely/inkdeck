#include "TreeListing.h"
#include <cstdio>
#include <cstdlib>
#include <string>
static void emit(void* ctx, const char* sha, const char* path) { auto* s=(std::string*)ctx; *s += sha; *s += ' '; *s += path; *s += '\n'; }
int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "rb"); std::string body; char b[4096]; size_t n;
  while ((n = fread(b,1,sizeof b,f))>0) body.append(b,n);
  std::string out; srand(1);
  auto* t = new TreeListing(emit, &out);
  for (size_t i=0;i<body.size();) { size_t c = 1 + rand()%700; if (i+c>body.size()) c=body.size()-i; t->feed(body.data()+i,c); i+=c; }
  fputs(out.c_str(), stdout);
  fprintf(stderr, "count=%u truncated=%d error=%d\n", t->count(), t->truncated(), t->hasError());
}
