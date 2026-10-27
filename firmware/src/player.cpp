#include "player.h"
#include <Audio.h>
#include <SD_MMC.h>
#include "config.h"
#include "settings.h"
#include "storage.h"

// The audio decoder runs in its own FreeRTOS task on core 0 so LVGL rendering,
// JPEG decoding and HTTP uploads on core 1 never starve it.
static Audio audio;
static SemaphoreHandle_t audioMutex;
static TaskHandle_t audioTask;
static std::vector<String> names;
static int idx = -1;
static bool active = false;
static bool pausedFlag = false;
static volatile bool trackEnded = false;

static void audioLoop(void *) {
  for (;;) {
    xSemaphoreTake(audioMutex, portMAX_DELAY);
    audio.loop();
    xSemaphoreGive(audioMutex);
    vTaskDelay(1);
  }
}

struct Lock {
  Lock() { xSemaphoreTake(audioMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(audioMutex); }
};

void player::init() {
  audioMutex = xSemaphoreCreateMutex();
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(settings.volume);
  refresh();
  xTaskCreatePinnedToCore(audioLoop, "audio", 8192, nullptr, 2, &audioTask, 0);
}

void player::refresh() {
  names = storage::list(MUSIC_DIR, {".mp3", ".wav", ".flac", ".aac", ".m4a"});
  if (idx >= (int)names.size()) idx = -1;
}

const std::vector<String> &player::files() {
  return names;
}

bool player::playIndex(int i) {
  if (i < 0 || i >= (int)names.size()) return false;
  String path = String(MUSIC_DIR) + "/" + names[i];
  Lock l;
  bool ok = audio.connecttoFS(SD_MMC, path.c_str());
  Serial.printf("[music] play %s: %s\n", path.c_str(), ok ? "ok" : "failed");
  if (ok) {
    idx = i;
    active = true;
    pausedFlag = false;
  }
  return ok;
}

bool player::play(const String &name) {
  for (size_t i = 0; i < names.size(); i++)
    if (names[i] == name) return playIndex(i);
  return false;
}

void player::stop() {
  Lock l;
  audio.stopSong();
  active = false;
  pausedFlag = false;
}

void player::pauseResume() {
  if (!active) return;
  Lock l;
  audio.pauseResume();
  pausedFlag = !pausedFlag;
}

void player::next() {
  if (names.empty()) return;
  playIndex((idx + 1) % names.size());
}

void player::prev() {
  if (names.empty()) return;
  playIndex((idx - 1 + names.size()) % names.size());
}

void player::setVolume(int v) {
  v = constrain(v, 0, 21);
  Lock l;
  audio.setVolume(v);
  settings.volume = v;
}

int player::volume() {
  return settings.volume;
}

bool player::playing() {
  return active;
}

bool player::isPaused() {
  return pausedFlag;
}

String player::currentName() {
  return (active && idx >= 0 && idx < (int)names.size()) ? names[idx] : String("");
}

int player::currentIndex() {
  return active ? idx : -1;
}

void player::loop() {
  if (trackEnded) {
    trackEnded = false;
    if (settings.musicRepeat || idx + 1 < (int)names.size()) next();
    else active = false;
  }
}

// ESP32-audioI2S callbacks (called from the audio task).
void audio_eof_mp3(const char *) {
  trackEnded = true;
}
void audio_info(const char *info) {
  Serial.printf("[audio] %s\n", info);
}
