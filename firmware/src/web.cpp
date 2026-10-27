#include "web.h"
#include <WebServer.h>
#include <FS.h>
#include <SD_MMC.h>
#include <time.h>
#include "config.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "slideshow.h"
#include "storage.h"
#include "ui.h"
#include "web_page.h"

static WebServer server(80);
static bool started = false;

// ---- upload state ----
static File uploadFile;
static String uploadPath;
static String uploadDir;
static size_t uploadBytes = 0;
static bool uploadOk = false;

static String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) continue;
    else o += c;
  }
  return o;
}

static void sendJson(const String &body, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendOk() {
  sendJson("{\"ok\":true}");
}

static void sendError(const char *msg, int code = 400) {
  sendJson(String("{\"ok\":false,\"error\":\"") + jsonEscape(msg) + "\"}", code);
}

// Resolve "picture" / "music" to an SD folder; nullptr for anything else.
static const char *dirFor(const String &d) {
  if (d == "picture") return PICTURE_DIR;
  if (d == "music") return MUSIC_DIR;
  return nullptr;
}

static bool isPicDir(const char *dir) {
  return strcmp(dir, PICTURE_DIR) == 0;
}

// File name from a request: must be a plain name, no path components.
static String safeName(const String &n) {
  if (n.isEmpty() || n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 || n.indexOf("..") >= 0) return "";
  return n;
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", WEB_PAGE);
}

static void handleStatus() {
  char tbuf[32] = "--:--";
  if (net::timeValid()) {
    time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    strftime(tbuf, sizeof tbuf, settings.clock24 ? "%H:%M" : "%I:%M %p", &t);
  }
  String j = "{";
  j += "\"version\":\"" FW_VERSION "\",";
  j += "\"host\":\"" DEVICE_HOSTNAME "\",";
  j += "\"ip\":\"" + net::ip() + "\",";
  j += "\"ssid\":\"" + jsonEscape(net::ssid()) + "\",";
  j += "\"rssi\":" + String(net::rssi()) + ",";
  j += "\"time\":\"" + String(tbuf) + "\",";
  j += "\"tz\":" + String(settings.tzIndex) + ",";
  j += "\"interval\":" + String(settings.intervalSec) + ",";
  j += "\"shuffle\":" + String(settings.shuffle ? "true" : "false") + ",";
  j += "\"showClock\":" + String(settings.showClock ? "true" : "false") + ",";
  j += "\"clock24\":" + String(settings.clock24 ? "true" : "false") + ",";
  j += "\"repeat\":" + String(settings.musicRepeat ? "true" : "false") + ",";
  j += "\"pictures\":" + String(slideshow::count()) + ",";
  j += "\"currentPicture\":\"" + jsonEscape(slideshow::currentName()) + "\",";
  j += "\"songs\":" + String(player::files().size()) + ",";
  j += "\"nowPlaying\":\"" + jsonEscape(player::currentName()) + "\",";
  j += "\"playing\":" + String(player::playing() ? "true" : "false") + ",";
  j += "\"paused\":" + String(player::isPaused() ? "true" : "false") + ",";
  j += "\"volume\":" + String(player::volume()) + ",";
  j += "\"sdTotal\":" + String((uint32_t)(storage::totalBytes() >> 20)) + ",";
  j += "\"sdUsed\":" + String((uint32_t)(storage::usedBytes() >> 20)) + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  j += "\"psram\":" + String(ESP.getFreePsram() / 1024) + ",";
  j += "\"uptime\":" + String(millis() / 1000);
  j += "}";
  sendJson(j);
}

static void handleList() {
  const char *dir = dirFor(server.arg("dir"));
  if (!dir) return sendError("bad dir");
  String j = "[";
  File root = SD_MMC.open(dir);
  bool first = true;
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        bool ok = isPicDir(dir) ? storage::isPicture(name) : storage::isMusic(name);
        if (ok && !name.startsWith(".")) {
          if (!first) j += ",";
          first = false;
          j += "{\"name\":\"" + jsonEscape(name) + "\",\"size\":" + String((uint32_t)f.size()) + "}";
        }
      }
      f = root.openNextFile();
    }
  }
  j += "]";
  sendJson(j);
}

static void handleFile() {
  const char *dir = dirFor(server.arg("dir"));
  String name = safeName(server.arg("name"));
  if (!dir || name.isEmpty()) return sendError("bad request");
  String path = String(dir) + "/" + name;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) return sendError("not found", 404);
  String lower = name;
  lower.toLowerCase();
  const char *type = "application/octet-stream";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) type = "image/jpeg";
  else if (lower.endsWith(".mp3")) type = "audio/mpeg";
  else if (lower.endsWith(".wav")) type = "audio/wav";
  server.sendHeader("Cache-Control", "max-age=3600");
  server.streamFile(f, type);
  f.close();
}

static void handleUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOk = false;
    uploadBytes = 0;
    uploadDir = server.arg("dir");
    const char *dir = dirFor(uploadDir);
    String name = storage::sanitizeName(server.hasArg("name") ? server.arg("name") : up.filename);
    if (!dir || !storage::ready()) return;
    bool typeOk = isPicDir(dir) ? storage::isPicture(name) : storage::isMusic(name);
    if (!typeOk) return;
    uploadPath = String(dir) + "/" + name;
    // Write to a temp name so a half-finished upload never shows up in the slideshow.
    if (SD_MMC.exists(uploadPath + ".part")) SD_MMC.remove(uploadPath + ".part");
    uploadFile = SD_MMC.open(uploadPath + ".part", FILE_WRITE);
    Serial.printf("[web] upload start %s\n", uploadPath.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadBytes + up.currentSize <= MAX_UPLOAD_BYTES) {
      uploadFile.write(up.buf, up.currentSize);
      uploadBytes += up.currentSize;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      if (SD_MMC.exists(uploadPath)) SD_MMC.remove(uploadPath);
      uploadOk = SD_MMC.rename(uploadPath + ".part", uploadPath);
      Serial.printf("[web] upload done %s (%u bytes)\n", uploadPath.c_str(), (unsigned)uploadBytes);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
      SD_MMC.remove(uploadPath + ".part");
    }
  }
}

static void handleUploadDone() {
  if (!uploadOk) return sendError("upload failed (wrong type, no SD card, or too large)");
  if (uploadDir == "picture") ui::picturesChanged();
  else ui::musicChanged();
  ui::toast(String("Received ") + uploadPath.substring(uploadPath.lastIndexOf('/') + 1));
  sendJson("{\"ok\":true,\"path\":\"" + jsonEscape(uploadPath) + "\"}");
}

static void handleDelete() {
  const char *dir = dirFor(server.arg("dir"));
  String name = safeName(server.arg("name"));
  if (!dir || name.isEmpty()) return sendError("bad request");
  String path = String(dir) + "/" + name;
  if (!isPicDir(dir) && player::currentName() == name) player::stop();
  if (!storage::remove(path)) return sendError("delete failed", 404);
  if (isPicDir(dir)) ui::picturesChanged();
  else ui::musicChanged();
  sendOk();
}

static void handleShow() {
  String name = safeName(server.arg("name"));
  if (!slideshow::show(name)) return sendError("cannot show picture", 404);
  sendOk();
}

static void handleSlideshow() {
  String cmd = server.arg("cmd");
  if (cmd == "next") slideshow::next();
  else if (cmd == "prev") slideshow::prev();
  else return sendError("bad cmd");
  sendOk();
}

static void handleMusic() {
  String cmd = server.arg("cmd");
  if (cmd == "play") {
    String name = safeName(server.arg("name"));
    if (name.isEmpty()) {
      if (player::files().empty()) return sendError("no music on the card");
      player::playIndex(player::currentIndex() < 0 ? 0 : player::currentIndex());
    } else if (!player::play(name)) return sendError("cannot play this file");
  } else if (cmd == "stop") player::stop();
  else if (cmd == "pause") {
    if (player::playing()) player::pauseResume();
    else if (!player::files().empty()) player::playIndex(0);
    else return sendError("no music on the card");
  } else if (cmd == "next") player::next();
  else if (cmd == "prev") player::prev();
  else if (cmd == "vol") {
    player::setVolume(server.arg("value").toInt());
    settings.save();
  } else return sendError("bad cmd");
  sendOk();
}

static void handleSettings() {
  if (server.hasArg("interval")) settings.intervalSec = constrain(server.arg("interval").toInt(), 3, 86400);
  if (server.hasArg("shuffle")) settings.shuffle = server.arg("shuffle").toInt() != 0;
  if (server.hasArg("showClock")) settings.showClock = server.arg("showClock").toInt() != 0;
  if (server.hasArg("clock24")) settings.clock24 = server.arg("clock24").toInt() != 0;
  if (server.hasArg("repeat")) settings.musicRepeat = server.arg("repeat").toInt() != 0;
  if (server.hasArg("tz")) settings.tzIndex = constrain(server.arg("tz").toInt(), 0, TIMEZONE_COUNT - 1);
  settings.save();
  slideshow::refresh();
  ui::settingsChanged();
  sendOk();
}

static void handleTime() {
  long epoch = server.arg("epoch").toInt();
  if (epoch < 1700000000L) return sendError("bad epoch");
  net::setTime(epoch);
  ui::settingsChanged();
  sendOk();
}

static void handleTimezones() {
  String j = "[";
  for (int i = 0; i < TIMEZONE_COUNT; i++) {
    if (i) j += ",";
    j += "\"" + jsonEscape(TIMEZONES[i].name) + "\"";
  }
  j += "]";
  sendJson(j);
}

void web::begin() {
  if (started) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/list", HTTP_GET, handleList);
  server.on("/api/timezones", HTTP_GET, handleTimezones);
  server.on("/file", HTTP_GET, handleFile);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/show", HTTP_POST, handleShow);
  server.on("/api/slideshow", HTTP_POST, handleSlideshow);
  server.on("/api/music", HTTP_POST, handleMusic);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/time", HTTP_POST, handleTime);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  started = true;
  Serial.println("[web] server started on port 80");
}

void web::loop() {
  if (started) server.handleClient();
}

bool web::running() {
  return started;
}
