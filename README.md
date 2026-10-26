# InkDeck

A super-minimal **Jot and markdown writing** firmware for the Xteink **X3/X4** (ESP32-C3
e-ink devices). Pair a Bluetooth HID input device, write `.jot` (or `.md`) files to the SD
card, and manage them — create/rename/delete files and folders — from the device
buttons or a connected keyboard.

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

- **Browser** — the SD card file list (folders first, then `.jot`, `.md`, `.markdown` and `.txt` files). Navigate
  with the device buttons or a connected keyboard's arrows/Enter; `Back` leaves a
  folder.
  - **New file / New folder / Rename / Delete** via the on-screen menu.
  - **Bluetooth** screen: scan, pair, connect, and forget HID devices.
- **Editor** — type markdown as plain text with soft word-wrap and a caret.
  - `Ctrl-S` save · `Esc` back to the browser (prompts if unsaved) · arrows /
    Home / End / PageUp / PageDown to move · Backspace / Delete to edit.
- **Name entry** (new file/folder, rename) **requires a connected keyboard** —
  you'll be prompted to pair one first.

## Jot files

New files are **`.jot`** unless you type another extension (`notes.md` stays markdown).
A `.jot` file is a plain-text [Jot v1](https://github.com/shelbeeely/.jot) document:
markdown plus a few journal marks, so everything you already type works. A new one
starts with its metadata block, titled from the file name:

```
---
Jot: 1.0
Title: 2026-10-26
---

```

Jot marks worth knowing on a keyboard: `[ ] task` / `[x] done`, `o 6:30p event`,
`? question`, `! idea`, and a `### 2026-10-26 | Monday` heading to start a day's entry
(several days can share one file).

**From the journal editor.** The `.jot` editor downloads your journal as `.jot` files to fill in:
each day a `### 2026-10-26 | Monday` entry with its blocks (`[ ] ` boxes to tick, `value:` blanks,
headings with room to write). Use **Download .jot, a file a week**: a whole month is a little over
the 16 KB InkDeck holds in its editor, and a bigger file opens cut short. Copy the files to the card
and fill them in on the device.

**Into the book.** The `.jot` builder (Journalwright's engine, in the `.jot` repository)
prints these notes into the monthly book, after the week they belong to. Copy the card's
files (or point at the card) and build:

```sh
cd journal && KW_JOTS=/path/to/card node render.mjs month 2026-10
```

An entry needs a date: name the file `YYYY-MM-DD.jot`, or add a `### YYYY-MM-DD`
heading. InkDeck has no clock it can trust, so it never fills the date in for you;
the build names any undated note and leaves it out.

**Packaged `.jot`** files (ZIP, with images or handwriting inside) are listed but not
opened: they are binary, and saving one as text would destroy it. Open those on a
computer.

## Building

This project consumes the FreeInk SDK via PlatformIO `symlink://` lib deps,
exactly like the `escape-hatch` example. For local development the committed
`platformio.local.ini` overrides the lib paths with absolute symlinks to a
working copy of the SDK at `/Users/jmitch/GitHub/freeink-sdk`, so SDK changes
iterate without committing anything.

```sh
pio run                 # build (env: default — X3 + X4 in one binary)
pio run -t upload       # flash over USB
pio device monitor      # serial @ 115200
```

The committed `platformio.ini` references the SDK as a `freeink-sdk/` checkout.
The `BleKeyboardHost` library and the FreeInkUI `textArea` component live in the
SDK working copy; once those are upstreamed, add the SDK as a submodule and the
committed config builds without the local override.

## Layout

```
src/
  main.cpp      lifecycle, input routing, refresh policy (FreeInkApp loop)
  AppState.h    screen enum + shared app state
  Document.*    the open file: fixed-capacity in-RAM text buffer + load/save
                (refuses ZIP-packaged .jot; new .jot files start from a Jot header)
  screens.*     browser, editor, file-ops menu, name entry, bluetooth screens
```
