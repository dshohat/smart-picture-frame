#pragma once
#include <Arduino.h>

struct TimeZoneInfo {
  const char *name;
  const char *posix;
};

extern const TimeZoneInfo TIMEZONES[];
extern const int TIMEZONE_COUNT;

struct Settings {
  String wifiSsid;
  String wifiPass;
  int tzIndex = 0;
  int intervalSec = 30;
  bool shuffle = false;
  bool showClock = true;
  bool clock24 = true;
  int volume = 12;        // 0..21 (ESP32-audioI2S scale)
  bool musicRepeat = true;

  void load();
  void save();
  const char *tzPosix() const;
};

extern Settings settings;
