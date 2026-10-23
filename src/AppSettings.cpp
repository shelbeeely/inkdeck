#include "AppSettings.h"

#include <Preferences.h>

AppSettings g_settings;

namespace {
constexpr const char* kPrefsNs = "inkdeck";
constexpr const char* kOrientationKey = "orient";
constexpr const char* kAppliedKey = "orientCfg";
}  // namespace

void AppSettings::load() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) return;
  orientation = prefs.getUChar(kOrientationKey, LandscapeClockwise);
  appliedSetting = prefs.getChar(kAppliedKey, -1);
  prefs.end();
  if (orientation > LandscapeCounterClockwise) orientation = LandscapeClockwise;
}

void AppSettings::save() const {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) return;
  prefs.putUChar(kOrientationKey, orientation);
  prefs.putChar(kAppliedKey, appliedSetting);
  prefs.end();
}

bool AppSettings::adoptSetting(int8_t value) {
  if (value < 0 || value > LandscapeCounterClockwise || value == appliedSetting) return false;
  appliedSetting = value;
  const bool changed = orientation != static_cast<uint8_t>(value);
  orientation = static_cast<uint8_t>(value);
  save();
  return changed;
}

freeink::ui::Orientation AppSettings::uiOrientation() const {
  switch (orientation) {
    case LandscapeClockwise:
      return freeink::ui::Orientation::LandscapeClockwise;
    case PortraitInverted:
      return freeink::ui::Orientation::PortraitInverted;
    case LandscapeCounterClockwise:
      return freeink::ui::Orientation::LandscapeCounterClockwise;
    case Portrait:
    default:
      return freeink::ui::Orientation::Portrait;
  }
}

const char* AppSettings::orientationLabel() const {
  switch (orientation) {
    case LandscapeClockwise:
      return "Landscape CW";
    case PortraitInverted:
      return "Inverted";
    case LandscapeCounterClockwise:
      return "Landscape CCW";
    case Portrait:
    default:
      return "Portrait";
  }
}
