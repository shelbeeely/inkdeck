#include "Companion.h"
#include <cstdio>
#include <cstring>
using namespace companion;
static int fails = 0;
#define CHECK(n, c) do { bool ok = (c); printf("%s %s\n", ok ? "PASS" : "FAIL", n); if (!ok) fails++; } while (0)
int main() {
  const char* doc =
      "---\ntype: InkDeckCompanion\nupdated: 2026-10-23T09:30:00-07:00\nlevel: 4\nxp: 963\nxp_level_start: 600\n"
      "xp_next: 1000\nstreak: 3\ngoal: 250\nlatest_achievement: \"One \\\"week\\\"\"\npet_name: \"Inkling\"\n"
      "pet_state: alive\npet_stage: child\npet_form: \npet_mood: hungry\npet_says: \"My tummy is rumbling...\"\n"
      "pet_fullness: 10\npet_fullness_base: 10\npet_food_per_100_words: 28.0\npet_happiness: 40\npet_health: 70\n"
      "---\n\n# Inkling (o_o)\n\nlevel: 99\n";
  Info c;
  CHECK("parse", parse(doc, strlen(doc), c) && c.valid);
  CHECK("numbers", c.level == 4 && c.xp == 963 && c.xpNext == 1000 && c.streak == 3 && c.goal == 250);
  CHECK("body ignored", c.level == 4);
  CHECK("strings", !strcmp(c.petName, "Inkling") && !strcmp(c.latestAchievement, "One \"week\"") && c.petForm[0] == 0);
  CHECK("food", c.foodPer100Words > 27.9f && c.foodPer100Words < 28.1f);
  CHECK("live fullness", liveFullness(c, 0) == 10 && liveFullness(c, 100) == 38 && liveFullness(c, 1000) == 100);
  CHECK("fed pet no longer hungry", !strcmp(liveMood(c, 0), "hungry") && !strcmp(liveMood(c, 100), "content"));
  uint16_t rows[16];
  sprite(c, "happy", rows);
  CHECK("sprite has a face", rows[6] != 0 && rows[9] != 0 && rows[14] != 0);
  Info e; strcpy(e.petState, "egg"); strcpy(e.petStage, "egg"); sprite(e, "egg", rows);
  CHECK("egg sprite", rows[0] == 0 && rows[1] == 0x03C0);
  Info bad;
  CHECK("not a companion file", !parse("---\ntype: Journal\n---\n", 21, bad));
  CHECK("no front matter", !parse("hello", 5, bad));
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails != 0;
}
