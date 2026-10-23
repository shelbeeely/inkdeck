#pragma once

#include <Arduino.h>
#include <FreeInkUI.h>

struct AppSettings {
  enum Orientation : uint8_t {
    Portrait = 0,
    LandscapeClockwise = 1,
    PortraitInverted = 2,
    LandscapeCounterClockwise = 3,
  };

  // Landscape by default: the panel's native shape, and how InkDeck is used
  // with a keyboard (wider lines, fewer wraps).
  uint8_t orientation = LandscapeClockwise;
  int8_t appliedSetting = -1;  // the `orientation` setting last adopted (see adoptSetting)

  void load();
  // Adopts the `orientation` setting (Config) when it differs from the one
  // adopted last time, so a choice made in the menu sticks until the setting
  // changes. Returns true if the orientation changed.
  bool adoptSetting(int8_t value);
  void save() const;
  freeink::ui::Orientation uiOrientation() const;
  const char* orientationLabel() const;
};

extern AppSettings g_settings;
