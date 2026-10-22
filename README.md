# InkDeck

A super-minimal **markdown writing** firmware for the Xteink **X3/X4** (ESP32-C3
e-ink devices). Pair a Bluetooth HID input device, write `.md` files to the SD
card, and manage them — create/rename/delete files and folders — from the device
buttons or a connected keyboard. Keep a daily journal and sync it with
[GitJournal](https://github.com/gitjournal/gitjournal) over Wi-Fi.

Built entirely on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk):
`FreeInkApp`/`FreeInkUI` for the UI, `SDCardManager` for storage, `XteinkDetect`
for X3/X4 auto-detection, and two pieces added to the SDK for this firmware:

- **`BleKeyboardHost`** (`libs/network/BleKeyboardHost`) — a BLE HID host for
  keyboards, page turners, and similar peripherals, capability-gated by
  `FREEINK_CAP_BLE_HID_HOST`. See the SDK's
  `docs/ble-keyboard-host.md`.
- **`textArea`** — a multi-line scrolling editor component in FreeInkUI.

> **Bluetooth is BLE only.** The ESP32-C3 has no Bluetooth Classic radio, so
> Classic-only keyboards cannot connect. Most modern wireless/compact keyboards
> and page turners are BLE.

## Using it

- **Browser** — the SD card file list (folders first, then `.md` files). Navigate
  with the device buttons or a connected keyboard's arrows/Enter; `Back` leaves a
  folder.
  - **New file / New folder / Rename / Delete** via the on-screen menu.
  - **Bluetooth** screen: scan, pair, connect, and forget HID devices.
- **Editor** — type markdown as plain text with soft word-wrap and a caret.
  - `Ctrl-S` save · `Esc` back to the browser (prompts if unsaved) · arrows /
    Home / End / PageUp / PageDown to move · Backspace / Delete to edit.
- **Name entry** (new file/folder, rename) **requires a connected keyboard** —
  you'll be prompted to pair one first.
- **Today's journal** (menu, or `T` in the browser) — opens `YYYY-MM-DD.md`
  for today in the notes folder, creating it with GitJournal front matter, and
  adds a `## HH:MM` heading for each writing session. Leaving without typing
  discards the heading (and doesn't create an empty entry).
- **Sync** (menu) — turns Bluetooth off, joins Wi-Fi, sets the clock over NTP,
  syncs the notes folder with your GitJournal repository on GitHub, then turns
  Wi-Fi off and Bluetooth back on.
- **Set date/time** (menu) — set the clock by hand (`YYYY-MM-DD HH:MM`) when
  there's no Wi-Fi.

## Journaling with GitJournal

[GitJournal](https://github.com/gitjournal/gitjournal) keeps notes as Markdown
files in a git repository. InkDeck writes entries the same way and syncs them
through the GitHub REST API, so entries written on the device show up in the
app (and vice versa).

1. Set up GitJournal with a **GitHub** repository (an existing one is fine).
   Optionally set *Settings → Journal → file name format* to **Date Only** so
   entries from the phone are named like InkDeck's (`2026-10-22.md`).
2. Create a [fine-grained personal access token](https://github.com/settings/personal-access-tokens/new)
   limited to that repository, with **Contents: Read and write**.
3. Pick **Sync** once: InkDeck creates `/inkdeck.txt` on the SD card. Fill it
   in on a computer, or open it in InkDeck's browser and edit it on the device:

   ```ini
   wifi_ssid = MyNetwork
   wifi_password = secret
   timezone = PST8PDT,M3.2.0,M11.1.0
   notes_folder = /Journal
   journal_subfolder =
   github_repo = you/journal
   github_branch = main
   github_token = github_pat_...
   ```

   `notes_folder` is the SD folder mirrored with the repo root;
   `journal_subfolder` is where Today's entries go inside it (empty = top
   level). Comments must be on their own line (`#` first), since passwords may
   contain `#`.

4. **Sync** again. The first run downloads the repository's `.md` files into
   the notes folder.

How sync works:

- Only `*.md` files are synced; other files and hidden folders are ignored.
- Each upload or delete is a separate commit (`InkDeck: update …`); GitJournal
  picks them up on its next pull.
- `<notes>/.inkdeck-sync` records each file's git blob id from the last sync,
  so InkDeck can tell edits made on the device, edits made in the app, and
  deletions apart. Deleting a note on either side deletes it on the other.
- If a note changed on **both** sides, nothing is lost: the device's version is
  kept as `<name> (InkDeck conflict).md` (here and on GitHub) next to the app's
  version.
- Changing `github_repo` or `github_branch` makes the next sync a merge: no
  deletions, and differing files are kept as conflict copies.

Notes and limits:

- The X4 has no clock chip. The time survives sleep (the ESP32-C3 keeps
  counting in deep sleep) but not a reset or a flat battery; Sync or Set
  date/time restores it.
- Sync needs 2.4 GHz Wi-Fi, and GitHub only (no GitLab/Gitea or SSH yet).
- Up to ~1000 synced notes. Each note opens up to `INKDECK_DOC_CAPACITY`
  (16 KB by default); larger files open truncated.
- `/inkdeck.txt` holds your Wi-Fi password and GitHub token in plain text on
  the SD card. Use a token limited to the journal repository.

## Building

This project consumes the FreeInk SDK via PlatformIO `symlink://` lib deps,
exactly like the `escape-hatch` example. The SDK is pinned as the `freeink-sdk/`
git submodule, so clone with submodules:

```sh
git clone --recurse-submodules <inkdeck-url>
# or, in an existing checkout:
git submodule update --init freeink-sdk
```

```sh
pio run                 # build (env: default — X3 + X4 in one binary)
pio run -t upload       # flash over USB
pio device monitor      # serial @ 115200
```

To bump the SDK, check out the new commit in the submodule and commit the
pointer:

```sh
git -C freeink-sdk fetch origin && git -C freeink-sdk checkout origin/main
git add freeink-sdk && git commit -m "Bump freeink-sdk"
```

For SDK development, an untracked `platformio.local.ini` can override the
`lib_deps` with absolute symlinks to a separate SDK working copy, so SDK changes
iterate without touching the submodule.

## Layout

```
src/
  main.cpp       lifecycle, input routing, refresh policy (FreeInkApp loop)
  AppState.h     screen enum + shared app state
  Document.*     the open file: fixed-capacity in-RAM text buffer + load/save
  screens.*      browser, editor, file-ops menu, name entry, bluetooth, sync screens
  Config.*       /inkdeck.txt settings (Wi-Fi, time zone, GitHub)
  Clock.*        NTP / manual clock, GitJournal timestamps
  Journal.*      today's entry (YYYY-MM-DD.md with front matter)
  SyncFlow.*     Sync screen work: Bluetooth off, Wi-Fi, NTP, GitHub, back on
  GitSync.*      two-way sync with a GitHub repo over the REST API
  TreeListing.*  streaming parser for GitHub's recursive tree listing
  SyncUtil.*     pure helpers: git blob ids, base64, escaping, sync decisions
```
