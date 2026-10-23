# InkDeck — status, feature list and backlog

This is the hand-off document. Anyone (person or agent) picking the project up
should be able to continue from here without the chat history. **Keep it
current: update it in the same commit as any feature change.**

Last updated: 2026-10-23, on branch `ccr-6bf7a05a-b0qao4` (not yet merged to `master`).

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
| `server/` | Python companion server (dashboard, stats; see §4) |
| `docs/phone-location.md` | BLE GATT contract for the phone app |
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

### 3.9 Settings keys (`/inkdeck.txt`, flasher, setup page)
`wifi_ssid`, `wifi_password`, `timezone`, `notes_folder`, `journal_subfolder`, `repo_url` (alias `github_repo`),
`branch` (alias `github_branch`), `git_token` (alias `github_token`), `location` (`phone`/`off`),
`location_every`, `writing_stats` (`on`/`off`). `#` is a comment only at line start.

## 4. Implemented features (server, `server/`)

- Python 3.10+ stdlib + PyYAML. `app.py` (HTTP + git refresher), `stats.py` (aggregation),
  `dashboard/index.html` (Chart.js + Leaflet), `sample_data.py`, `test_stats.py`, Dockerfile, compose.
- Keeps a shallow clone, pulls every `PULL_INTERVAL` s. Token passed as an `http.extraHeader`, never stored.
- Endpoints: `/`, `/api/stats`, `POST /api/refresh`, `/healthz`. Optional basic auth (`DASHBOARD_PASSWORD`).
- Env: `REPO_URL`, `GIT_TOKEN`, `GIT_USERNAME`, `NOTES_PATH`, `NOTES_SUBFOLDER`, `PULL_INTERVAL`, `DASHBOARD_PASSWORD`, `PORT`.
- Docker image has **not** been built/tested (no Docker daemon in the dev environment).

## 5. Not yet implemented (backlog, in priority order)

Direction from the user: **"The intermediary server needs to handle as much of this as possible."**
So: the server computes and writes; the X4 records raw data and displays results.

### 5.1 Server becomes a companion that writes back (in progress)
- Optional write access (`WRITE_BACK=1`): commit + push to the journal repo as "InkDeck Companion".
- Output file the device pulls through normal sync: `_inkdeck/companion.md` — front matter with the
  pet, level/XP, streaks, today's goal progress, newest achievements (simple `key: value` lines the X4 can parse),
  plus a readable body for GitJournal.
- To avoid sync conflicts with the X4, the server edits a journal entry **only after that day is over and the
  file hasn't changed for 2 h**; today's data lives only in `companion.md`.

### 5.2 Weather / sun / air quality enrichment (server-side)
- Open-Meteo forecast + air-quality APIs (no key, `timezone=auto`, single day, ≤ ~90 days back; older days
  via the archive API `archive-api.open-meteo.com/v1/archive`).
- Per session: `weather: {temp_c, feels_c, humidity, cloud, precip_mm, wind_kmh, condition, aqi, pm25, light}`
  where `light` is "dark" / "sunrise" / "golden hour" / "sunset" / "X after sunrise" / "X before sunset".
- Per entry: top-level `day:` block (sunrise, sunset, daylight_h, temp max/min, precip, uv_max, weather, moon, `final`, `source`).
- Location: session `location:`, else `home_location = lat,lon` (setting / server env `HOME_LOCATION`).
- The firmware module `src/OpenMeteo.*` already implements the same output on-device (tested against real
  responses in `test/host/fixtures/`). It is kept as a fallback for running without a server; not wired in.

### 5.3 Gamification (server computes, X4 shows)
- Daily word goal (`daily_goal`, default 250) — progress in the editor header, toast on reaching it (X4 can compute today's
  words locally; the server confirms).
- XP: words/10 + 10 per session ≥5 min + 15 per check-in + 25 per goal day + 50 per achievement.
  Level L needs `50·L·(L−1)` XP (0, 100, 300, 600, 1000 …).
- Achievements: first words; streaks 3/7/30/100; words 1k/10k/50k/100k; early bird (before 7:00); night owl (after 23:00);
  marathon (session ≥45 min); sprinter (≥40 wpm, ≥100 words); check-ins 7; check-in week; reflector (3-2-1 done 7×);
  goal 7 days; weekend writer; rain writer, sunrise writer, explorer (5 places) — server-only (need weather/location).
- Rest days (`rest_days` per week, default 1) protect streaks.

### 5.4 Journaling pet companion (Tamagotchi-like) — requested
- Kept alive by writing. Fully **derived from journal history** (deterministic replay; no hidden state), so the
  server can recompute it any time and the X4 can show it.
- Design: see `server/companion.py` once written; summary in §5.1/§7 of this file when done.

### 5.5 Check-ins (morning / evening) — device
- Templates `<notes>/_inkdeck/checkins/morning.txt`, `evening.txt` (synced). Format: `title:`, `when: HH:MM-HH:MM`,
  fields `key | type | label`; types `scale`, `number`, `time`, `count`, `check`, `choice a,b,c`, `text`, `list N`.
- Default fields: sleep_h, bedtime, wake, rest, energy, caffeine, sugar, meds/vitamins, PT, symptoms, where, social,
  win, stress, response, learned×3, enjoyed×2 (3-2-1 reflection), focus.
- Stored as front-matter `checkins:` list items (flow maps; helper `findListItem` exists and is tested), readable
  answers in the body. Usage-log `checkin` event. Setting `checkins = on|off`; menu item "Check-in".
- Server: check-in trends and correlations (sleep/energy/caffeine/weather/place/AQI vs words) on the dashboard.

### 5.6 Other environmental data
- Phone **ambient** characteristic `ff3f0002-53df-4bf1-9794-eac66727467a`, value `1;key=value;…` → session `ambient:`
  (parser `journalmeta::parseAmbient` done + tested; BLE read not wired; doc not written).
- **Place tags**: setting `place = Name; lat,lon; radius_m` (repeatable) → session `place_tag:` (field exists; matching not written).
  Could be done by the server instead.

### 5.7 Everything else
- Other git providers (Gitea/Forgejo/GitLab/Bitbucket/Azure REST) or a git smart-HTTP bridge on the server.
- Server as a git bridge (device talks only to the server; server talks to any provider).
- Link picker (`[[` / Ctrl-K), follow link (Ctrl-Enter), backlinks; folder-rename link updates.
- Wi-Fi-scan location; location settings on the on-device Setup page.
- OTA updates from the server.
- Phone app implementing the GATT server (location + ambient).
- Enable GitHub Pages (Settings → Pages → Source: GitHub Actions) after merging to `master`.
- Build/test the Docker image.
- **Test on real X4 hardware** (RAM headroom during TLS + BLE, sync, display).

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
