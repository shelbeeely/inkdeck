#include "SyncUtil.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>
using namespace syncutil;
int main(int argc,char**argv){
  // blob sha of argv[1] content
  std::string c = argv[1];
  Sha1 s; blobShaBegin(s,c.size()); for(size_t i=0;i<c.size();i+=7) sha1Update(s,c.data()+i,std::min<size_t>(7,c.size()-i));
  uint8_t d[20]; sha1Final(s,d); char hex[41]; toHex(d,hex); printf("%s\n",hex);
  char b[4096]; size_t n=base64Encode((const uint8_t*)c.data(),c.size(),b); b[n]=0; printf("%s\n",b);
  char u[128]; assert(urlEscapePath("Journal/a b+ü.md",u,sizeof u)); printf("%s\n",u);
  char j[64]; assert(jsonEscape("a\"b\\\n",j,sizeof j)); printf("%s\n",j);
  const char*A="a",*B="b",*C="c";
  assert(decide(A,A,nullptr)==Action::None); assert(decide(A,B,A)==Action::Pull); assert(decide(B,A,A)==Action::Push);
  assert(decide(B,C,A)==Action::Conflict); assert(decide(A,B,nullptr)==Action::Conflict);
  assert(decide(nullptr,A,A)==Action::DeleteRemote); assert(decide(nullptr,B,A)==Action::Pull); assert(decide(nullptr,A,nullptr)==Action::Pull);
  assert(decide(A,nullptr,A)==Action::DeleteLocal); assert(decide(B,nullptr,A)==Action::Push); assert(decide(A,nullptr,nullptr)==Action::Push);
  assert(isSyncedNotePath("Journal/2026-10-22.md") && !isSyncedNotePath(".git/x.md") && !isSyncedNotePath("a/.hidden/x.md") && !isSyncedNotePath("a.txt"));
  // 1MB-ish sha sanity on long content
  puts("ok");
}
