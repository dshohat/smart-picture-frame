// Smart picture frame for the Freenove FNK0086 ESP32-S3 Development Kit.
//
// Boot -> (saved Wi-Fi? connect : Wi-Fi setup screen) -> slideshow + clock.
// Pictures and music are uploaded from a phone over Wi-Fi (see web.cpp) and
// stored on the microSD card; the touch screen offers a settings menu.

#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "slideshow.h"
#include "storage.h"
#include "ui.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[frame] Smart picture frame v%s\n", FW_VERSION);

  settings.load();
  display::init();
  storage::init();
  net::init();
  slideshow::init();
  player::init();
  ui::init();

  if (!settings.wifiSsid.isEmpty()) {
    ui::showConnecting(settings.wifiSsid);
    net::connect(settings.wifiSsid, settings.wifiPass);
  } else {
    ui::showWifiSetup(true);
  }
  Serial.printf("[frame] ready, heap %u KB, psram %u KB\n", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
}

void loop() {
  display::loop();
  net::loop();

  if (net::justConnected()) {
    web::begin();
    ui::toast("Connected: http://" + net::ip());
    if (!ui::mainVisible()) ui::showMain();
  }
  if (net::state() == net::State::Failed) {
    // Shown once; the user picks retry / another network / offline.
    ui::showConnectFailed(settings.wifiSsid);
    net::disconnect();
  }

  ui::loop();
  slideshow::loop();
  player::loop();
  web::loop();
  delay(2);
}
