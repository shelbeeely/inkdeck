#include "LinkRewrite.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace linkrewrite;
static int fails = 0;
static void t(const char* name, const char* note, const char* in, const char* expect, int expectChanged,
              const char* oldP = "Ideas/Old Idea.md", const char* newP = "Ideas/Better Idea.md", bool unique = true) {
  Rename r{oldP, newP, unique};
  char out[4096]; size_t n = 0;
  int c = rewrite(in, strlen(in), note, r, out, sizeof out, n);
  std::string got(out, n);
  bool ok = got == expect && c == expectChanged;
  if (!ok) fails++;
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) printf("   in:   %s\n   got:  %s (%d)\n   want: %s (%d)\n", in, got.c_str(), c, expect, expectChanged);
}
int main() {
  t("bare wiki", "Journal/2026-10-23.md", "See [[Old Idea]] today.", "See [[Better Idea]] today.", 1);
  t("bare wiki with .md", "a.md", "[[Old Idea.md]]", "[[Better Idea.md]]", 1);
  t("wiki alias + heading kept", "a.md", "[[Old Idea#Plan|the plan]]", "[[Better Idea#Plan|the plan]]", 1);
  t("wiki spaces kept", "a.md", "[[ Old Idea ]]", "[[ Better Idea ]]", 1);
  t("wiki with path", "Journal/x.md", "[[Ideas/Old Idea]] and [[ideas/Old Idea]]", "[[Ideas/Better Idea]] and [[ideas/Old Idea]]", 1);
  t("bare wiki skipped when name not unique", "a.md", "[[Old Idea]] [[Ideas/Old Idea]]", "[[Old Idea]] [[Ideas/Better Idea]]", 1,
    "Ideas/Old Idea.md", "Ideas/Better Idea.md", false);
  t("case must match (like GitJournal)", "a.md", "[[old idea]]", "[[old idea]]", 0);
  t("other notes untouched", "a.md", "[[Old Ideas]] [[Older Idea]] [[Old]]", "[[Old Ideas]] [[Older Idea]] [[Old]]", 0);
  t("md link relative, encoded", "Journal/x.md", "[it](../Ideas/Old%20Idea.md#Plan)", "[it](../Ideas/Better%20Idea.md#Plan)", 1);
  t("md link same folder, no ext", "Ideas/y.md", "[it](Old%20Idea)", "[it](Better%20Idea)", 1);
  t("md link from root", "Journal/x.md", "[it](/Ideas/Old%20Idea.md)", "[it](/Ideas/Better%20Idea.md)", 1);
  t("md link angle brackets", "Journal/x.md", "[it](<../Ideas/Old Idea.md> \"title\")", "[it](<../Ideas/Better Idea.md> \"title\")", 1);
  t("md link wrong folder untouched", "Journal/x.md", "[it](Old%20Idea.md)", "[it](Old%20Idea.md)", 0);
  t("new name needs encoding", "Ideas/y.md", "[it](old.md)", "[it](new%20name.md)", 1, "Ideas/old.md", "Ideas/new name.md");
  t("reference definition", "Journal/x.md", "[p]: ../Ideas/Old%20Idea.md", "[p]: ../Ideas/Better%20Idea.md", 1);
  t("URL untouched", "Ideas/y.md", "[w](https://x.com/Ideas/Old%20Idea.md)", "[w](https://x.com/Ideas/Old%20Idea.md)", 0);
  t("inline code untouched", "a.md", "`[[Old Idea]]` vs [[Old Idea]]", "`[[Old Idea]]` vs [[Better Idea]]", 1);
  t("fenced code untouched", "a.md", "```\n[[Old Idea]]\n```\n[[Old Idea]]\n", "```\n[[Old Idea]]\n```\n[[Better Idea]]\n", 1);
  t("front matter untouched", "a.md", "---\ntitle: \"[[Old Idea]]\"\n---\n[[Old Idea]]", "---\ntitle: \"[[Old Idea]]\"\n---\n[[Better Idea]]", 1);
  t("several links on a line", "Ideas/z.md", "[[Old Idea]], [a](Old%20Idea.md) and [[Ideas/Old Idea|x]]",
    "[[Better Idea]], [a](Better%20Idea.md) and [[Ideas/Better Idea|x]]", 3);
  t("image untouched", "Ideas/z.md", "![pic](photo.png)", "![pic](photo.png)", 0);
  t("climbing above root ignored", "a.md", "[x](../../Ideas/Old%20Idea.md)", "[x](../../Ideas/Old%20Idea.md)", 0);
  // overflow reports -1
  { Rename r{"a.md", "b.md", true}; char out[4]; size_t n; int c = rewrite("[[a]] text", 10, "x.md", r, out, sizeof out, n);
    printf("%s overflow detected\n", c == -1 ? "PASS" : "FAIL"); if (c != -1) fails++; }
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails != 0;
}
