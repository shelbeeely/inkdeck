#pragma once

// The journaling pet, levels and streaks, as worked out by the InkDeck server
// (server/companion.py) and synced down in `<notes>/_inkdeck/companion.md`.
// The X4 only displays it. Pure (no Arduino/SD), host-testable.
//
// The file's front matter is flat "key: value" lines, e.g.
//   level: 4
//   pet_name: "Inkling"
//   pet_fullness_base: 30          # fullness before today's writing
//   pet_food_per_100_words: 28.0   # what 100 words today add

#include <stddef.h>
#include <stdint.h>

namespace companion {

constexpr const char* kPath = "/_inkdeck/companion.md";  // under the notes folder

struct Info {
  bool valid = false;
  char updated[32] = {0};
  int32_t level = 1, xp = 0, xpLevelStart = 0, xpNext = 100;
  int32_t streak = 0, bestStreak = 0, restDaysLeft = 0, goal = 250, goalStreak = 0;
  int32_t achievements = 0, achievementsTotal = 0;
  char latestAchievement[48] = {0};
  char petName[24] = {0};
  char petState[8] = {0};   // egg | alive | asleep | gone
  char petStage[8] = {0};   // egg | baby | child | teen | adult
  char petForm[12] = {0};   // adult form: scholar, wanderer, ...
  char petMood[8] = {0};    // egg | happy | content | hungry | sad | sick | asleep | gone
  char petSays[96] = {0};
  char hatchProgress[8] = {0};  // "2/3"
  int32_t petAgeDays = 0, petGeneration = 1;
  int32_t fullness = 0, fullnessBase = 0, happiness = 0, health = 0;
  float foodPer100Words = 0;
};

// Parses the file (only the front matter is read). False if it isn't one.
bool parse(const char* text, size_t len, Info& out);

// Fullness now: the server's figure from before today plus what today's words add.
int32_t liveFullness(const Info& c, int32_t todayWords);

// The mood to show: the server's, except that writing today can feed a
// hungry pet before the next sync.
const char* liveMood(const Info& c, int32_t todayWords);

// 16x16 sprite for the stage and mood: one row per uint16_t, bit 15 = leftmost.
void sprite(const Info& c, const char* mood, uint16_t rows[16]);

}  // namespace companion
