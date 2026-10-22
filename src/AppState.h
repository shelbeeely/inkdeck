#pragma once

// Shared application state for InkDeck. One global instance drives every screen
// function (which are pure render-from-state). Fixed-capacity throughout to keep
// peak RAM predictable on the ESP32-C3.

#include <Arduino.h>
#include <FreeInkUI.h>
#include <FreeInkUIDisplayTarget.h>

#include "Document.h"

enum class AppScreen : uint8_t {
  Boot,
  Browser,
  Editor,
  Menu,
  Orientation,
  NameEntry,
  Bluetooth,
  Sleep,
  ConfirmDelete,
  ConfirmDiscard,
  Sync,
};

enum class NamePurpose : uint8_t {
  NewFile,
  NewFolder,
  Rename,
  SetClock,
};

struct AppState {
  static constexpr int kMaxEntries = 96;
  static constexpr int kBtRows = 1 + 4 + 24;  // scan row + bonds + discovered (BleKeyboardHost::kMaxDiscovered)

  AppScreen screen = AppScreen::Browser;

  // --- Browser ---
  char path[160] = "/";
  char names[kMaxEntries][64];  // display label; folders end with '/'
  bool isDir[kMaxEntries];
  freeink::ui::ListItem items[kMaxEntries];
  uint16_t entryCount = 0;
  int browserSel = 0;
  uint16_t browserTop = 0;
  uint16_t browserVisible = 1;
  int menuTarget = -1;  // browser entry captured for rename/delete

  // --- Editor ---
  Document doc;
  uint32_t journalStub = 0;  // hash of a just-opened journal entry (0 = not one)
  int16_t edBodyW = 100;
  int16_t edLineH = 20;
  uint16_t edVisible = 1;

  // --- Menu ---
  int menuSel = 0;

  // --- Orientation ---
  int orientationSel = 0;

  // --- Name entry ---
  NamePurpose namePurpose = NamePurpose::NewFile;
  char nameBuf[64] = {0};
  uint16_t nameLen = 0;
  uint16_t nameCursor = 0;

  // --- Bluetooth ---
  int btSel = 0;
  uint16_t btTop = 0;
  uint16_t btVisible = 1;
  char btLabel[kBtRows][56];
  char btAddr[kBtRows][18];
  uint8_t btKind[kBtRows];  // 0 = scan action, 1 = connectable device
  freeink::ui::ListItem btItems[kBtRows];
  uint8_t btRowCount = 0;

  // --- Sync ---
  static constexpr int kSyncLines = 12;
  char syncLines[kSyncLines][64];
  uint8_t syncLineCount = 0;
  bool syncDone = false;
  bool pendingToday = false;  // open today's journal once the clock is set

  // --- Chrome ---
  char statusLabel[40] = {0};
  char toast[64] = {0};
  uint32_t toastUntil = 0;
  bool lastConnected = false;
};

extern AppState g_state;
extern freeink::ui::DisplayTarget* g_target;
extern freeink::ui::ThemeTokens g_theme;
