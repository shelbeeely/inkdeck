# Tests

## Host tests (automated)

`test/host/run.sh` compiles InkDeck's pure modules (no Arduino, SD or radio)
with g++, runs them, checks the YAML they write with PyYAML, and runs the
server's unit tests. Needs `g++`, `python3` and `PyYAML`, and the
`freeink-sdk` submodule (for the JSON parser).

| Test | Covers |
|---|---|
| `journalmeta_test.cpp` | phone location parse, session YAML insert/replace, GitJournal-style reindented lists, word/paragraph counting |
| `enrich_test.cpp` | walking sessions, adding/replacing session lines and top-level blocks (idempotent), `checkins:` list, ambient parse |
| `openmeteo_test.cpp` | Open-Meteo parsing against real responses (`fixtures/wx.json`, `aq.json`), sun labels, moon phase, YAML |
| `usage_test.cpp` | usage CSV format/parse, streaks, totals, typing counters |
| `linkrewrite_test.cpp` | link rewriting on rename (wiki + markdown links) |
| `syncutil_test.cpp`, `url_test.cpp` | blob SHA-1, base64, escaping, sync decision table, repo URL parsing |
| `companion_test.cpp` | parsing the server's `companion.md`, live fullness/mood, sprites |
| `tree_test.cpp` | streaming git-tree parser on a real GitHub response, random chunking |

## Manual harnesses

These were used during development and need a little setup; paths inside may
assume the repo is at `/home/user/inkdeck`.

- `web/` — Playwright scripts (Chromium at `/opt/pw-browsers/chromium`):
  flasher page with a fake serial device (`flasher_page_test*.mjs`), the
  device setup page, and screenshots (`screenshot.mjs <url> <out.png> <width> [dark]`,
  `dashboard_screenshot.mjs`). Serve `web/` with `python -m http.server`.
- `improv/` — compiles `src/Improv.cpp` against stubs into `./harness`, then
  `settings_test.mjs` drives it through `web/settings.js` over a pipe;
  `test.mjs` drives it with `improv-wifi-serial-sdk` (npm) to check Wi-Fi provisioning.
- `render/` — renders screens with the real FreeInkUI on the host to PGM
  (convert to PNG with Pillow). `harness.cpp`: the Stats screen.
  `pet_harness.cpp <notes folder> <words today> <out.pgm>`: the Pet screen
  from `<notes folder>/_inkdeck/companion.md`. Build from `test/render`:

  ```sh
  g++ -std=c++17 -O1 -I pet_stub -I stub -I ../../src -I ../../freeink-sdk/libs/ui/FreeInkUI/include \
    pet_harness.cpp ../../src/PetScreen.cpp ../../src/Companion.cpp ../../src/UsageStats.cpp \
    ../../src/JournalMeta.cpp ../../freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp -o pet
  # Stats: harness.cpp ../../src/StatsScreen.cpp ../../src/UsageStats.cpp ../../src/JournalMeta.cpp + FreeInkUI.cpp (no pet_stub)
  ```
