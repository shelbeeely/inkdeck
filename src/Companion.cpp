#include "Companion.h"

#include <stdlib.h>
#include <string.h>

#include <initializer_list>

namespace companion {

namespace {

// Copies a value, removing surrounding quotes and \" / \\ escapes.
void copyValue(const char* v, size_t n, char* out, size_t outsz) {
  size_t o = 0;
  if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
    ++v;
    n -= 2;
  }
  for (size_t i = 0; i < n && o + 1 < outsz; ++i) {
    if (v[i] == '\\' && i + 1 < n) ++i;
    out[o++] = v[i];
  }
  out[o] = '\0';
}

void apply(Info& c, const char* key, size_t klen, const char* v, size_t vlen) {
  char num[24];
  copyValue(v, vlen, num, sizeof(num));
  const int32_t n = static_cast<int32_t>(atol(num));
  auto is = [&](const char* k) { return strlen(k) == klen && memcmp(k, key, klen) == 0; };
  if (is("type")) c.valid = strcmp(num, "InkDeckCompanion") == 0;
  else if (is("updated")) copyValue(v, vlen, c.updated, sizeof(c.updated));
  else if (is("level")) c.level = n;
  else if (is("xp")) c.xp = n;
  else if (is("xp_level_start")) c.xpLevelStart = n;
  else if (is("xp_next")) c.xpNext = n;
  else if (is("streak")) c.streak = n;
  else if (is("best_streak")) c.bestStreak = n;
  else if (is("rest_days_left")) c.restDaysLeft = n;
  else if (is("goal")) c.goal = n;
  else if (is("goal_streak")) c.goalStreak = n;
  else if (is("achievements")) c.achievements = n;
  else if (is("achievements_total")) c.achievementsTotal = n;
  else if (is("latest_achievement")) copyValue(v, vlen, c.latestAchievement, sizeof(c.latestAchievement));
  else if (is("pet_name")) copyValue(v, vlen, c.petName, sizeof(c.petName));
  else if (is("pet_state")) copyValue(v, vlen, c.petState, sizeof(c.petState));
  else if (is("pet_stage")) copyValue(v, vlen, c.petStage, sizeof(c.petStage));
  else if (is("pet_form")) copyValue(v, vlen, c.petForm, sizeof(c.petForm));
  else if (is("pet_mood")) copyValue(v, vlen, c.petMood, sizeof(c.petMood));
  else if (is("pet_says")) copyValue(v, vlen, c.petSays, sizeof(c.petSays));
  else if (is("pet_hatch_progress")) copyValue(v, vlen, c.hatchProgress, sizeof(c.hatchProgress));
  else if (is("pet_age_days")) c.petAgeDays = n;
  else if (is("pet_generation")) c.petGeneration = n;
  else if (is("pet_fullness")) c.fullness = n;
  else if (is("pet_fullness_base")) c.fullnessBase = n;
  else if (is("pet_happiness")) c.happiness = n;
  else if (is("pet_health")) c.health = n;
  else if (is("pet_food_per_100_words")) c.foodPer100Words = static_cast<float>(atof(num));
}

}  // namespace

bool parse(const char* text, size_t len, Info& out) {
  out = Info();
  if (len < 4 || memcmp(text, "---\n", 4) != 0) return false;
  size_t p = 4;
  while (p < len) {
    size_t e = p;
    while (e < len && text[e] != '\n') ++e;
    size_t lineEnd = e;
    if (lineEnd > p && text[lineEnd - 1] == '\r') --lineEnd;
    if (lineEnd - p == 3 && memcmp(text + p, "---", 3) == 0) return out.valid;
    const char* colon = static_cast<const char*>(memchr(text + p, ':', lineEnd - p));
    if (colon && text[p] != ' ' && text[p] != '#') {
      const char* v = colon + 1;
      while (v < text + lineEnd && *v == ' ') ++v;
      size_t vlen = static_cast<size_t>(text + lineEnd - v);
      while (vlen && v[vlen - 1] == ' ') --vlen;
      apply(out, text + p, static_cast<size_t>(colon - (text + p)), v, vlen);
    }
    p = e + 1;
  }
  return false;  // no closing ---
}

int32_t liveFullness(const Info& c, int32_t todayWords) {
  if (strcmp(c.petState, "alive") != 0) return c.fullness;
  const int32_t base = c.fullnessBase;
  const float add = todayWords > 0 ? todayWords * c.foodPer100Words / 100.0f : 0;
  const int32_t now = base + static_cast<int32_t>(add + 0.5f);
  // The server may have seen more of today than this device has (another
  // device, or words since the last sync on this one): never show less.
  const int32_t v = now > c.fullness ? now : c.fullness;
  return v > 100 ? 100 : v;
}

const char* liveMood(const Info& c, int32_t todayWords) {
  if (strcmp(c.petMood, "hungry") == 0 && liveFullness(c, todayWords) >= 25) return "content";
  return c.petMood[0] ? c.petMood : "content";
}

namespace {

// Sprites, '#' = ink. The creature's face is drawn over the body.
constexpr const char* kEgg[16] = {
    "................", "......####......", "....##....##....", "...#........#...",
    "..#..........#..", "..#...#..#...#..", ".#............#.", ".#....##......#.",
    ".#............#.", ".#..#.....#...#.", ".#............#.", "..#....#.....#..",
    "..#..........#..", "...#........#...", "....##....##....", "......####......",
};
constexpr const char* kBody[16] = {
    "................", "................", ".....######.....", "...##......##...",
    "..#..........#..", ".#............#.", ".#............#.", ".#............#.",
    ".#............#.", ".#............#.", ".#............#.", "..#..........#..",
    "...##......##...", ".....######.....", "....##....##....", "................",
};
// Ears for teens and adults (rows 0-3).
constexpr const char* kEars[4] = {"................", "...##......##...", "..#..#....#..#..", "................"};

uint16_t bits(const char* row) {
  uint16_t v = 0;
  for (int i = 0; i < 16; ++i) v = static_cast<uint16_t>(v | ((row[i] == '#') ? (0x8000 >> i) : 0));
  return v;
}

void set(uint16_t rows[16], int x, int y) {
  if (x >= 0 && x < 16 && y >= 0 && y < 16) rows[y] = static_cast<uint16_t>(rows[y] | (0x8000 >> x));
}

}  // namespace

void sprite(const Info& c, const char* mood, uint16_t rows[16]) {
  const bool egg = strcmp(c.petState, "egg") == 0 || strcmp(c.petStage, "egg") == 0;
  if (egg) {
    for (int y = 0; y < 16; ++y) rows[y] = bits(kEgg[y]);
    return;
  }
  for (int y = 0; y < 16; ++y) rows[y] = bits(kBody[y]);
  if (strcmp(c.petStage, "teen") == 0 || strcmp(c.petStage, "adult") == 0) {
    for (int y = 0; y < 4; ++y) rows[y] = static_cast<uint16_t>(rows[y] | bits(kEars[y]));
  }
  // Eyes (left at x 4-5, right at x 10-11, rows 6-7) and mouth (rows 9-10).
  auto eyes = [&](bool top, bool bottom) {
    for (int x : {4, 5, 10, 11}) {
      if (top) set(rows, x, 6);
      if (bottom) set(rows, x, 7);
    }
  };
  auto mouth = [&](const char* r9, const char* r10) {  // 6 columns from x 5
    for (int i = 0; i < 6; ++i) {
      if (r9[i] == '#') set(rows, 5 + i, 9);
      if (r10[i] == '#') set(rows, 5 + i, 10);
    }
  };
  if (strcmp(mood, "happy") == 0) {
    eyes(true, false);
    for (int x : {3, 6, 9, 12}) set(rows, x, 7);  // ^ ^
    mouth("#....#", ".####.");
  } else if (strcmp(mood, "hungry") == 0) {
    eyes(true, true);
    mouth("..##..", "..##..");
  } else if (strcmp(mood, "sad") == 0) {
    eyes(false, true);
    mouth(".####.", "#....#");
  } else if (strcmp(mood, "sick") == 0) {
    for (int d : {0, 1}) {  // x x
      set(rows, 4 + d, 6 + d);
      set(rows, 5 - d, 6 + d);
      set(rows, 10 + d, 6 + d);
      set(rows, 11 - d, 6 + d);
    }
    mouth("......", "#.#.#.");
  } else if (strcmp(mood, "asleep") == 0 || strcmp(mood, "gone") == 0) {
    eyes(false, true);
    mouth("......", "..##..");
    if (strcmp(mood, "asleep") == 0) {
      for (int x = 12; x < 15; ++x) set(rows, x, 1), set(rows, x, 3);  // a small "z"
      set(rows, 13, 2);
    }
  } else {  // content
    eyes(true, true);
    mouth("......", ".####.");
  }
  if (strcmp(c.petStage, "baby") == 0) {
    // Babies are rounder: no feet yet.
    rows[14] = 0;
  }
}

}  // namespace companion
