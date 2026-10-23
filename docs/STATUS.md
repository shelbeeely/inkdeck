# InkDeck — status, feature list and backlog

This is the hand-off document. Anyone (person or agent) picking the project up
should be able to continue from here without the chat history. **Keep it
current: update it in the same commit as any feature change.**

Last updated: 2026-10-23 (journaling prompts), on branch `ccr-6bf7a05a-b0qao4` (not yet merged to `master`).

**Resume here:** the next item is §5.1 (check-ins on the X4), then §5.2. Run `test/host/run.sh` and
`pio run` first to confirm a green baseline.

---

## 1. What InkDeck is

Minimal markdown-writing firmware for the **Xteink X4** (and X3), an ESP32-C3
e-ink device, used with a **Bluetooth LE keyboard**. It is a daily journal that
syncs to a git repo in **GitJournal** format, records rich writing statistics,
and has a companion **server** (dashboard + enrichment + gamification).

- Hardware: ESP32-C3 (single 2.4 GHz radio, ~320 KB RAM, no PSRAM), 16 MB flash, SD card, e-ink.
- Built on the **FreeInk SDK** (`freeink-sdk/` git submodule, Free-Ink/freeink-sdk @ `28edea3`).
- PlatformIO, pioarduino `espressif32 55.03.37` (Arduino-ESP32 3.3.7), NimBLE-Arduino 2.3.8+.
- Nothing has been tested on real X4 hardware yet. Everything was verified on
  the host (see §6). **Hardware testing is the top open risk.**

## 2. Repository map

| Path | What |
|---|---|
| `src/main.cpp` | setup/loop, Improv serial, settings handler, usage-log hooks, sleep |
| `src/screens.cpp/.h`, `src/AppState.h` | all UI screens (browser, editor, menu, name entry, Sync, Setup, Stats) |
| `src/StatsScreen.cpp` | Stats screen on the X4 |
| `prompts/library.txt` | the journaling prompt library (84 prompts, 13 categories, each with a "why"); compiled into the firmware and read by the server |
| `src/Prompts.*`, `src/PromptScreen.cpp` | **pure** prompt-library parser / today's prompt / entry text; the prompt picker screen |
| `tools/prompts_header.py` | pre-build: `prompts/library.txt` → `.pio/build/prompts/PromptLibrary.h` |
| `src/Companion.*`, `src/PetScreen.cpp` | **pure** parser for the server's `companion.md` + pet sprites; the Pet screen |
| `src/Config.*` | settings (layers, keys, NVS storage) |
| `src/Clock.*` | time zone, NTP, ISO formatting |
| `src/Journal.*` | daily entry (open today, front matter, `## HH:MM` session headings) |
| `src/Document.*` | front-matter editing helpers on the open document |
| `src/JournalMeta.*` | **pure**: session YAML, counters, location parse, front-matter list/block splicing, ambient parse |
| `src/WritingStats.*` | per-session writing stats, written into the entry's front matter |
| `src/PhoneLocation.*` | BLE GATT client that reads the phone's location |
| `src/GitSync.*`, `src/SyncFlow.*`, `src/SyncUtil.*`, `src/TreeListing.*` | two-way GitHub sync |
| `src/Net.*` | Wi-Fi up/down (pauses Bluetooth) |
| `src/Improv.*` | Improv Serial + custom RPC 0x80 for settings from the web flasher |
| `src/SetupFlow.*`, `src/SetupServer.*` | on-device setup web page (QR code) |
| `src/Links.*`, `src/LinkRewrite.*` | update wiki/markdown links when a note is renamed |
| `src/UsageLog.*`, `src/UsageStats.*` | whole-device usage CSV log and its stats (streaks etc.) |
| `src/OpenMeteo.*` | **pure**, tested, **not wired in**: Open-Meteo request paths, streaming parser, YAML output (see §5.2) |
| `tools/flash_config.py` | bakes `flash-config.txt` / `INKDECK_*` env into `FlashConfig.h` |
| `tools/merge_bin.py` | writes `.pio/build/default/inkdeck-full.bin` (single image for offset 0) |
| `tools/build_web_flasher.py` | assembles the web flasher site |
| `web/` | web flasher (ESP Web Tools 10.4.0 + settings over Improv) |
| `.github/workflows/web-flasher.yml` | builds firmware + deploys flasher to GitHub Pages on push to `master` |
| `server/` | Python server: dashboard, stats, companion (pet/XP/achievements), weather enrichment, write-back (§4) |
| `docs/phone-location.md` | BLE GATT contract for the phone app |
| `docs/img/` | screenshots (host renders) |
| `test/host/` | host test suite (`test/host/run.sh`) |
| `test/web`, `test/improv`, `test/render` | browser/Node/render harnesses (manual; see `test/README.md`) |

## 3. Implemented features (firmware)

### 3.1 Editor and files
- File browser, plain-text markdown editor, create/rename/delete files and folders, BLE keyboard pairing.
- **Rename updates links** (`Links::afterRename`): rewrites `[[name]]`, `[[name|alias]]`,
  `[[path/name]]` and `[text](rel/path.md)` links in every note under the notes folder.
  Name-only wiki links are only rewritten when the old name was unique. Folder renames do **not** update links yet.

### 3.2 Daily journal
- Menu "Today" opens `<notes>/<journal_subfolder>/YYYY-MM-DD.md` (GitJournal Journal format:
  YAML front matter `created`, `modified`, `type: Journal`). A new entry isn't written until you type.
- Each opening adds a `## HH:MM` heading (a "session"). `modified` updated on save.

### 3.3 Time
- NTP when Wi-Fi is up (`Clock`), POSIX `timezone` string, manual set fallback.

### 3.4 Git sync (GitHub only)
- Two-way sync via the GitHub REST API over TLS 1.2 (framework CA bundle):
  git trees (`?recursive=1`), contents GET (raw) / PUT / DELETE.
- State: git blob SHA-1 per file in `<notes>/.inkdeck-sync` (first line `# repo branch`).
  Decision table in `syncutil::decide` (pull / push / delete either side / conflict).
- Synced paths (`syncutil::isSyncedNotePath`): `*.md` not under dot-folders, plus `_inkdeck/usage/*.csv`.
- Wi-Fi and BLE share one radio: Bluetooth is paused (`BleHid.end`) while Wi-Fi is up (RAM).

### 3.5 Setup / provisioning
- **Flash-time config**: `flash-config.txt` (gitignored; see `flash-config.example.txt`) or `INKDECK_*` env vars, baked into the image.
- **Web flasher** (`web/`): ESP Web Tools + Improv Wi-Fi; custom Improv RPC `0x80` sends the other settings
  as `key = value` lines in ≤253-byte packets (`web/settings.js`). "Send settings only" button for already-flashed devices.
- **On-device setup page** (`SetupServer`): Wi-Fi AP or station + QR code; edit settings in a browser.
- Settings layering: defaults < baked `kFlashConfig` < NVS (`inkcfg`/`cfg`) < `/inkdeck.txt` on SD. Empty values never override.

### 3.6 Writing stats (per session, in front matter)
Per session: start, end, active_s, keys, words, wpm, typed, deleted, pauses, longest_pause_s,
longest_burst_s, first_key_s, revisions, location, and `parts:` (one per paragraph: start, end, active_s, words).
Format documented at the top of `src/JournalMeta.h`. Also supports `place_tag:` and `ambient:` lines
(fields exist in `Session`; nothing fills them yet — see backlog).

### 3.7 Phone location over BLE
- X4 is a GATT **client**; phone app is the server. Contract: `docs/phone-location.md`.
- Service `ff3f25e1-53df-4bf1-9794-eac66727467a`, location char `ff3f0001-…` value `1;lat;lon;accuracy;unix;place`.
- Settings `location = phone`, `location_every` (min, default 15). No phone app exists yet.

### 3.8 Usage log + Stats screen
- CSV per month `<notes>/_inkdeck/usage/YYYY-MM.csv`, header
  `time,event,path,duration_s,active_s,keys,typed,deleted,words,wpm,battery,detail`. Events: sessions, syncs, keyboard, etc.
- Stats screen on the X4: today/week/month words, streak & best streak, best day, hour buckets, syncs, keyboard time.

### 3.9 Pet screen (display only)
- *Menu → Pet* reads `<notes>/_inkdeck/companion.md` (written by the server, synced like any note) and shows the
  pet as a 16×16 pixel sprite (bigger per stage; ears from teen; face per mood), its line, fullness/happiness/health
  bars, level bar, streak, today's words vs goal, badges, the server's top **insight**, and the last update
  (if there's room).
- **Live feeding**: fullness = `pet_fullness_base + today's words × pet_food_per_100_words / 100` (never below the
  server's figure); a hungry pet shows as content once today's words lift it to 25+. Today's words come from the usage log.
- Without the file it explains how to set up the server. Parser + sprites: `src/Companion.*` (host-tested).

### 3.10 Journaling prompts (user request: about 40% of people don't know where to start; 32% see no benefit)
- **Picker** (`PromptScreen.cpp`) opens over the editor:
  - when Today opens and the setting `prompts` allows it: `ask` (default) = when the day's body is only the
    `## HH:MM` heading (≤2 words); `always` = every session; `off` = never;
  - **Cmd/Ctrl+Shift+P** in the editor;
  - *Menu → Journal prompt* (opens Today, then the picker).
- **Top level**:
  - "Today's prompt" (`prompts::dailyIndex(days since 1970, count)`; stride 37 coprime with the count, so every
    prompt comes up once per cycle, identical in `server/prompts.py`);
  - "For you" (the server's `_inkdeck/prompts-suggested.txt`);
  - "Surprise me";
  - built-in categories with counts;
  - "Your prompts" (`_inkdeck/prompts.txt`, ≤6 KB).
- **Inside a category**, its prompts. A bottom panel shows the selected prompt in full and its **"Why: …"** line.
- **Choosing a prompt**:
  - inserts `> **Prompt:** <text>` at the caret, starting a new paragraph;
  - calls `WritingStats::addPrompt(id)`, so the session gets `prompts: [id, …]` in front matter;
  - re-stubs the entry (`Journal::stubHash`), so a fresh session with only a prompt and no writing is still
    discarded on leaving.
- The library is compiled in (≈11.6 KB flash); heap is used only while the picker is open (≈10 KB plus files).
- Tested on the host: `test/host/prompts_test.cpp` (real library validity, parsing, daily cycle, entry text,
  session YAML) and a host render (`test/render/prompt_harness.cpp`, image `docs/img/prompt-picker.png`).

### 3.11 Settings keys (`/inkdeck.txt`, flasher, setup page)
`wifi_ssid`, `wifi_password`, `timezone`, `notes_folder`, `journal_subfolder`, `repo_url` (alias `github_repo`),
`branch` (alias `github_branch`), `git_token` (alias `github_token`), `location` (`phone`/`off`),
`location_every`, `writing_stats` (`on`/`off`), `prompts` (`ask`/`always`/`off`). `#` is a comment only at line start.

## 4. Implemented features (server, `server/`)

Direction from the user: **"The intermediary server needs to handle as much of this as possible."** The server
computes; the X4 records raw data and displays results.

- Python 3.10+ stdlib + PyYAML. Files:
  - `app.py`: HTTP server + git refresher + write-back.
  - `stats.py`: aggregation; also check-ins, session weather, `day:` blocks.
  - `companion.py`: XP, levels, achievements, streaks and the pet. It is pure and deterministic.
  - `enrich.py`: Open-Meteo enrichment.
  - `dashboard/index.html`.
  - `sample_data.py`, which includes weather and check-ins.
  - Tests: `test_stats.py`, `test_companion.py`, `test_enrich.py`.
  - Docker: `Dockerfile` and `compose.yaml`.
- Keeps a shallow clone, pulls every `PULL_INTERVAL` s. Token passed as an `http.extraHeader`, never stored.
- Endpoints: `/`, `/api/stats` (now includes `companion`, `checkins`, `weather_days`), `POST /api/refresh`, `/healthz`.
- **Write-back** (`WRITE_BACK=1`): per refresh:
  1. pull (`reset --hard` to the remote);
  2. enrich finished entries;
  3. recompute;
  4. write `_inkdeck/companion.md` only if it changed beyond its `updated:` line;
  5. commit as "InkDeck Companion" and `push origin HEAD:<branch>`.
  If the push is rejected (the X4 pushed meanwhile), the next refresh redoes the work from the new head.
  Tested end to end against a local bare repo.
- **Companion rules** (`companion.py` docstring + `server/README.md`):
  - XP: words/10 + 10 per session of ≥5 active minutes + 15 per check-in + 5 per prompt answered (session ≥30
    words) + 25 per goal day + 50 per achievement.
  - Level L needs 50·L·(L−1) XP.
  - Streak: `REST_DAYS` misses per ISO week are forgiven. Today never breaks a streak.
  - 25 achievements (list in `ACHIEVEMENTS`, including the prompt ones: prompted_5, curious = 6 categories,
    time_traveler = a `revisit-` prompt), each with its unlock date.
  - **Pet**:
    - An egg arrives with the first writing day and hatches after 3 writing days.
    - Stages by days since hatching: baby, child 7, teen 21, adult 60.
    - Adult form: scholar, wanderer, chatterbox, early_bird, night_owl or dreamer.
    - Fullness: the daily goal = +70; −40 each night.
    - Happiness: check-ins, streak ≥3, goal, achievements; −20 for a day without writing, −5 otherwise.
    - Health: −25 a night at 0 fullness; +10 when fullness ≥40.
    - At 0 health: in `gentle` mode it hibernates (3 writing days to revive); in `classic` mode it is lost
      (kept in history) and a new egg comes with the next entry.
    - Moods: egg, happy, content, hungry, sad, sick, asleep, gone, each with an ASCII face and lines.
- **Prompts** (`prompts.py`, tests in `test_prompts.py`):
  - Library: `prompts/library.txt` (`PROMPTS_LIBRARY` overrides) plus the repo's `_inkdeck/prompts.txt`.
  - **Usage**: per prompt and category, from session `prompts:`; prompted vs plain journal sessions' average words.
  - **Insights**, only with enough data and only when the result holds (never a claim the data doesn't show):
    - evening check-in `energy`/`stress`/`mood` on journaling vs non-journaling days (≥4 each, ≥0.3 apart, in
      the good direction);
    - prompted vs plain words (≥3 each, ≥10% more);
    - the best category (≥2 categories used twice);
    - days journaled out of the last 14;
    - the word total.
    The first insight becomes `companion.md`'s `insight:`; all appear in its body and on the dashboard.
  - **Suggestions** (deterministic per day, skip prompts used in the last 14 days, max 5, at least 3):
    1. look back at the entry from 365/182/30/7 days ago, quoting its first real sentence (id `revisit-<date>`);
    2. latest check-in (≤2 days old): stress ≥4 → stress; sleep_h <6 → body-sleep; energy ≤2 → body;
       social = 0 → people;
    3. the best category;
    4. an untried category;
    5. quick/start prompts to fill up.
    Written to `_inkdeck/prompts-suggested.txt` (library format, category `foryou`) with write-back, and
    committed only when changed (about once a day).
  - Dashboard **Prompts** card: insights, today's prompts with reasons, prompted vs plain tiles, the most used
    prompts. The day view shows each session's prompt.
  - Docker: `compose.yaml` builds from the repo root (`.dockerignore` keeps only `server/` and the library).
- **Weather** (`enrich.py`):
  - Only touches entries whose day is over and whose `modified` is ≥2 h old, so it never collides with the X4.
  - Adds a session `weather:` flow map and a top-level `day:` block (`final: true`). The rest of the file is byte-identical.
  - Location: session location, else `HOME_LOCATION`.
  - Source: the forecast API for ≤85 days back; the archive API for older days.
  - Air quality: best effort.
  - Responses cached in `CACHE_PATH`; 1 s pacing; one retry on timeout; stops at HTTP 429.
- **Dashboard**:
  - Companion card: pet face, bars, level/XP, streak, goal, rest days.
  - Achievements grid.
  - Day view shows the day's weather/sun/moon, each session's weather, and check-ins.
  - Verified at desktop and phone widths, light and dark, with no overflow.
- Env (all documented in `.env.example` and `app.py`):
  - Git and access: `REPO_URL`, `GIT_TOKEN`, `GIT_USERNAME`, `BRANCH`, `NOTES_PATH`, `NOTES_SUBFOLDER`.
  - Server: `PULL_INTERVAL`, `DASHBOARD_PASSWORD`, `PORT`, `TZ`.
  - Companion and weather: `WRITE_BACK`, `WEATHER`, `HOME_LOCATION`, `CACHE_PATH`, `DAILY_GOAL`, `REST_DAYS`,
    `PET_NAME`, `PET_MODE`.
- Docker image **not** built/tested (no Docker daemon in the dev environment). The Dockerfile installs `tzdata`.
- Open-Meteo rate-limited the dev container's shared IP (429). The archive API was verified live; the forecast
  API's response format is verified by the fixture `test/host/fixtures/wx.json`, captured from it earlier.

## 5. Not yet implemented (backlog, in priority order)

### 5.1 Check-ins (morning / evening) — device input, server already reads them
- The server already parses front-matter `checkins:` items, gives XP and achievements for them, and the
  dashboard shows them in the day view. **Nothing on the X4 writes them yet.**
- Templates:
  - Files `<notes>/_inkdeck/checkins/morning.txt` and `evening.txt`, synced.
  - Format: `title:`, `when: HH:MM-HH:MM`, and fields `key | type | label`.
  - Field types: `scale`, `number`, `time`, `count`, `check`, `choice a,b,c`, `text`, `list N`.
- Default fields:
  - Sleep and body: sleep_h, bedtime, wake, rest, energy, caffeine, sugar, meds/vitamins, PT, symptoms.
  - Day: where, social, win, stress, response, focus.
  - Reflection (3-2-1): learned×3, enjoyed×2.
- Storage:
  - Front matter: a `checkins:` list item as a flow map, e.g. `{time: <iso>, kind: morning|evening, key: value…}`.
    The helper `journalmeta::findListItem` exists and is tested.
  - **Quote times** (`bedtime: "23:30"`), because YAML 1.1 reads 23:30 as a base-60 int.
  - Text answers go in the body; the flow map keeps only `win: true` and counts such as `learned: 3`.
    The server's "Reflector" badge needs `kind: evening` plus a truthy `learned`.
- Also: a usage-log `checkin` event, setting `checkins = on|off`, and a menu item "Check-in".
- A prompt shows when Today opens inside a template's `when` window; it can be skipped.
- Server: check-in trends and correlations on the dashboard (sleep/energy/caffeine/weather/place/AQI vs words).

### 5.2 Small device-side gamification touches
- Daily goal progress in the editor header and a toast on reaching it. The goal comes from `companion.md`'s `goal:`;
  today's words from the usage log/WritingStats.
- Toast on sync when `latest_achievement` changed ("New badge: …").
- Optional: pet face in the browser header or sleep screen.

### 5.2b Prompt follow-ups (ideas, not built)
- Show the "For you" reason on the Pet screen or as a toast after sync ("A year ago today you wrote…").
- Number keys 1–9 to pick a row in the picker; type to filter prompts.
- A monthly "look back" page from the server (best entries, most-used prompts, check-in trends) as a note in the repo.
- Edit and add prompts from the dashboard (it would write `_inkdeck/prompts.txt`).
- The `prompts` setting on the on-device Setup page (it's on the web flasher and in `/inkdeck.txt`).

### 5.3 Other environmental data
- Phone **ambient** characteristic `ff3f0002-53df-4bf1-9794-eac66727467a`, value `1;key=value;…`, to session `ambient:`.
  The parser `journalmeta::parseAmbient` is done and tested; the BLE read is not wired, and the doc section is not written.
- **Place tags**: named places (`Name; lat,lon; radius_m`) to session `place_tag:`. This is better done on the
  server (env `PLACES`), writing `place_tag` during enrichment. Not written.
- Device-side weather fallback without a server: `src/OpenMeteo.*` is ready (tested), not wired into SyncFlow.

### 5.4 Everything else
- Other git providers (Gitea/Forgejo/GitLab/Bitbucket/Azure REST), or the server as a git bridge for the X4.
- Link picker (`[[` / Ctrl-K), follow link (Ctrl-Enter), backlinks, and link updates when a folder is renamed.
- Wi-Fi-scan location, and location settings on the on-device Setup page.
- OTA firmware updates from the server.
- Phone app implementing the GATT server (location + ambient).
- Enable GitHub Pages (Settings → Pages → Source: GitHub Actions) after merging to `master`.
- Build and test the Docker image.
- **Test on real X4 hardware** (RAM headroom during TLS + BLE, sync, display, the Pet screen).

## 6. How to build and test

```sh
git submodule update --init
pio run                       # firmware; image at .pio/build/default/inkdeck-full.bin
test/host/run.sh              # host tests: pure C++ modules + server unit tests (needs g++, python3, PyYAML)
cd server && python -m unittest -q
```
Other harnesses (flasher page in Chromium/Playwright, Improv RPC against the real SDK, Stats screen render to PNG)
are in `test/web`, `test/improv`, `test/render`; see `test/README.md`.

## 7. Environment quirks (cloud dev container)

- PlatformIO: `pip install platformio==6.1.19` into `/root/.platformio/penv` (with `uv`); GitHub archive URLs may be blocked.
  Set `SSL_CERT_FILE=/root/.ccr/ca-bundle.crt`, and append the proxy CA to certifi's `cacert.pem` if pip/pio fail TLS.
- Chromium for Playwright: `/opt/pw-browsers/chromium` (don't run `playwright install`).
- No Docker daemon.
- Don't `pkill -f` a pattern that matches your own shell.
- `namespace sync` clashes with POSIX `sync()` — the namespace is `syncutil`.

## 8. Decisions log (why things are the way they are)

- Phone location: custom BLE GATT server on the phone (user's choice) rather than BTGPS/NMEA.
- Config `#` is a comment only at line start (passwords contain `#`).
- New journal entry isn't saved until typing (avoids empty files syncing).
- Server reads via shallow clone with header auth; token never written to disk.
- Server does heavy lifting (weather, gamification, pet); firmware stays lean (RAM, radio time).
- Pet, XP and achievements are **derived by replaying the journal history** (no stored state): reproducible,
  survives server restarts, and the rules can change without migrations.
- The server edits only **finished, quiet** entries (day over, unchanged for 2 h), so it can't conflict with the
  X4's two-way sync. Today's state lives in `_inkdeck/companion.md`, which only the server writes.
- `companion.md` front matter is flat `key: value` so the X4 needs no YAML parser. It is re-committed only when
  something besides `updated:` changes, to avoid needless syncs.
- The X4 adds today's words to the pet's fullness locally, so feeding is visible before the next sync.
- Pet default is `gentle` (hibernates rather than dies); `classic` is opt-in.
- Prompts: one library file, compiled into the firmware (it works offline, with no server) and read by the
  server, so ids and "today's prompt" agree everywhere. Every prompt has a "why" because the user's second
  barrier is not seeing the benefit. Personal insights are descriptive ("on days you journal…"), never causal
  claims, and are hidden until the data supports them.
- A prompt goes into the entry as a visible quote (readable in GitJournal), and as an id in the session record
  (for the stats).
