#include <initializer_list>
#include "SyncUtil.h"
#include <cstdio>
#include <cstring>
int main(){
  const char* cases[][3] = {
    {"https://github.com/you/journal.git","github.com","you/journal"},
    {"https://github.com/you/journal/","github.com","you/journal"},
    {"git@github.com:you/journal.git","github.com","you/journal"},
    {"ssh://git@gitea.example.org:2222/you/notes.git","gitea.example.org","you/notes"},
    {"https://user@GitLab.com/group/sub/notes.git","gitlab.com","group/sub/notes"},
    {"github.com/you/journal","github.com","you/journal"},
    {"you/journal","github.com","you/journal"},
  };
  int bad=0; char h[64], r[100];
  for (auto& c: cases){ bool ok=syncutil::parseRepoUrl(c[0],h,sizeof h,r,sizeof r);
    if(!ok||strcmp(h,c[1])||strcmp(r,c[2])){printf("FAIL %s -> %d %s %s\n",c[0],ok,h,r);bad++;} }
  for (const char* b: {"", "journal", "https://github.com/", "github.com/you"}) if (syncutil::parseRepoUrl(b,h,sizeof h,r,sizeof r)) {printf("FAIL accepted %s\n",b);bad++;}
  puts(bad?"FAILED":"ok");
}
