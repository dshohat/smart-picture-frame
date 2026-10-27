#pragma once
#include <Arduino.h>

namespace ui {
void init();
void loop();

// Screens
void showWifiSetup(bool allowSkip);
void showConnecting(const String &ssid);
void showConnectFailed(const String &ssid);
void showMain();
void showMenu();
bool mainVisible();

// Notifications from other modules
void toast(const String &msg, uint32_t ms = 2500);
void picturesChanged();
void musicChanged();
void settingsChanged();
}
