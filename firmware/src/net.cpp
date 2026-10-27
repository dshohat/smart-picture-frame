#include "net.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "settings.h"

static net::State st = net::State::Idle;
static uint32_t connectStart = 0;
static bool connectedFlag = false;
static bool userConnect = false;  // connection requested by the user/boot flow (vs. background reconnect)
static bool scanning = false;
static std::vector<net::ScanResult> lastScan;
static bool mdnsStarted = false;

void net::init() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  applyTimezone();
}

void net::connect(const String &ssid, const String &pass) {
  Serial.printf("[net] connecting to '%s'\n", ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  connectStart = millis();
  userConnect = true;
  st = State::Connecting;
}

void net::disconnect() {
  WiFi.disconnect(true);
  st = State::Idle;
}

void net::setOffline() {
  WiFi.disconnect(true);
  st = State::Offline;
}

net::State net::state() {
  return st;
}

bool net::justConnected() {
  bool v = connectedFlag;
  connectedFlag = false;
  return v;
}

String net::ip() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
}

String net::ssid() {
  return WiFi.SSID();
}

int net::rssi() {
  return WiFi.RSSI();
}

void net::startScan() {
  if (scanning) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  scanning = true;
}

bool net::scanFinished() {
  if (!scanning) return true;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return false;
  lastScan.clear();
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (s.isEmpty()) continue;
      bool dup = false;
      for (auto &r : lastScan)
        if (r.ssid == s) {
          dup = true;
          if (WiFi.RSSI(i) > r.rssi) r.rssi = WiFi.RSSI(i);
          break;
        }
      if (!dup) lastScan.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
    std::sort(lastScan.begin(), lastScan.end(), [](const ScanResult &a, const ScanResult &b) { return a.rssi > b.rssi; });
  }
  WiFi.scanDelete();
  scanning = false;
  return true;
}

std::vector<net::ScanResult> net::scanResults() {
  return lastScan;
}

bool net::timeValid() {
  time_t now = time(nullptr);
  return now > 1700000000;  // after Nov 2023 => not the 1970 default
}

void net::applyTimezone() {
  setenv("TZ", settings.tzPosix(), 1);
  tzset();
}

void net::setTime(time_t epoch) {
  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
}

void net::loop() {
  if (st == State::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      st = State::Connected;
      if (userConnect) connectedFlag = true;
      userConnect = false;
      Serial.printf("[net] connected, ip %s\n", WiFi.localIP().toString().c_str());
      if (!mdnsStarted && MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
      }
      configTzTime(settings.tzPosix(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    } else if (millis() - connectStart > WIFI_CONNECT_TIMEOUT_MS) {
      if (userConnect) {
        Serial.println("[net] connect timeout");
        WiFi.disconnect();
        userConnect = false;
        st = State::Failed;
      } else {
        // Lost connection in the background: keep retrying quietly.
        connectStart = millis();
        WiFi.reconnect();
      }
    }
  } else if (st == State::Connected && WiFi.status() != WL_CONNECTED) {
    st = State::Connecting;
    connectStart = millis();
  }
}
