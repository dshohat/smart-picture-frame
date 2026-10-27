#pragma once
#include <Arduino.h>
#include <vector>

namespace net {
enum class State { Idle, Connecting, Connected, Failed, Offline };

struct ScanResult {
  String ssid;
  int rssi;
  bool secure;
};

void init();
void loop();

void connect(const String &ssid, const String &pass);  // async; poll state()
void disconnect();
void setOffline();  // user chose to skip Wi-Fi
State state();
bool justConnected();  // true once, right after a successful connection
String ip();
String ssid();
int rssi();

void startScan();
bool scanFinished();
std::vector<ScanResult> scanResults();

bool timeValid();
void applyTimezone();
void setTime(time_t epoch);
}
