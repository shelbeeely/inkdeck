#pragma once

// Whole-device usage log (see UsageStats.h for the format): every editing
// session of any file, plus creates/renames/deletes, syncs, boots and sleeps
// with the battery level, and keyboard connections. Appended to
// <notes>/_inkdeck/usage/YYYY-MM.csv, which syncs with the journal. Entries
// need a set clock; without one they're skipped.

#include <Arduino.h>

#include "Document.h"
#include "UsageStats.h"

namespace UsageLog {

void boot();   // after the SD card and settings are up
void sleep();  // just before deep sleep

// An editing session of the file open in `doc`.
void editBegin(const Document& doc, bool journal);
void edit(const Document& doc, uint32_t inserted, uint32_t removed);
void editEnd(const Document& doc);  // logs the session if anything changed

void fileEvent(const char* event, const char* fullPath, const char* detail = "");  // create/rename/delete
void sync(uint32_t durationS, const char* detail);
void keyboard(bool connected, const char* name);

int batteryPercent();  // -1 when unknown

// Reads every month's log into `stats` for the Stats screen. `today` gets
// the local day number. False without a clock.
bool load(usage::Stats& stats, int32_t& today);

}  // namespace UsageLog
