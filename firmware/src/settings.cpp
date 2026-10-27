#include "settings.h"
#include <Preferences.h>

Settings settings;

const TimeZoneInfo TIMEZONES[] = {
  {"Israel", "IST-2IDT,M3.4.4/26,M10.5.0"},
  {"UTC", "UTC0"},
  {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Paris / Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Athens / Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Moscow", "MSK-3"},
  {"Dubai", "<+04>-4"},
  {"India", "IST-5:30"},
  {"Bangkok", "<+07>-7"},
  {"Beijing / Singapore", "CST-8"},
  {"Tokyo", "JST-9"},
  {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"Sao Paulo", "<-03>3"},
  {"New York (ET)", "EST5EDT,M3.2.0,M11.1.0"},
  {"Chicago (CT)", "CST6CDT,M3.2.0,M11.1.0"},
  {"Denver (MT)", "MST7MDT,M3.2.0,M11.1.0"},
  {"Los Angeles (PT)", "PST8PDT,M3.2.0,M11.1.0"},
  {"Hawaii", "HST10"},
};
const int TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

static Preferences prefs;

void Settings::load() {
  prefs.begin("frame", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  tzIndex = constrain(prefs.getInt("tz", 0), 0, TIMEZONE_COUNT - 1);
  intervalSec = prefs.getInt("interval", 30);
  shuffle = prefs.getBool("shuffle", false);
  showClock = prefs.getBool("showClock", true);
  clock24 = prefs.getBool("clock24", true);
  volume = constrain(prefs.getInt("volume", 12), 0, 21);
  musicRepeat = prefs.getBool("repeat", true);
  prefs.end();
}

void Settings::save() {
  prefs.begin("frame", false);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putInt("tz", tzIndex);
  prefs.putInt("interval", intervalSec);
  prefs.putBool("shuffle", shuffle);
  prefs.putBool("showClock", showClock);
  prefs.putBool("clock24", clock24);
  prefs.putInt("volume", volume);
  prefs.putBool("repeat", musicRepeat);
  prefs.end();
}

const char *Settings::tzPosix() const {
  return TIMEZONES[constrain(tzIndex, 0, TIMEZONE_COUNT - 1)].posix;
}
