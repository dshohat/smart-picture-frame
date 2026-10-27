#include "storage.h"
#include <FS.h>
#include <SD_MMC.h>
#include <algorithm>
#include "config.h"

static bool mounted = false;

static void ensureDir(const char *path) {
  if (!SD_MMC.exists(path)) SD_MMC.mkdir(path);
}

bool storage::init() {
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  mounted = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 5);
  if (!mounted) {
    Serial.println("[sd] mount failed");
    return false;
  }
  ensureDir(PICTURE_DIR);
  ensureDir(MUSIC_DIR);
  Serial.printf("[sd] mounted, %llu MB used of %llu MB\n", SD_MMC.usedBytes() >> 20, SD_MMC.totalBytes() >> 20);
  return true;
}

bool storage::ready() {
  return mounted;
}

static bool hasExt(const String &lower, std::initializer_list<const char *> exts) {
  for (const char *e : exts)
    if (lower.endsWith(e)) return true;
  return false;
}

std::vector<String> storage::list(const char *dir, std::initializer_list<const char *> exts) {
  std::vector<String> out;
  if (!mounted) return out;
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) return out;
  File f = root.openNextFile();
  while (f && out.size() < MAX_FILES) {
    if (!f.isDirectory()) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      String lower = name;
      lower.toLowerCase();
      if (!name.startsWith(".") && hasExt(lower, exts)) out.push_back(name);
    }
    f = root.openNextFile();
  }
  root.close();
  std::sort(out.begin(), out.end(), [](const String &a, const String &b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  return out;
}

bool storage::remove(const String &path) {
  return mounted && SD_MMC.remove(path);
}

bool storage::isPicture(const String &name) {
  String l = name;
  l.toLowerCase();
  return hasExt(l, {".jpg", ".jpeg"});
}

bool storage::isMusic(const String &name) {
  String l = name;
  l.toLowerCase();
  return hasExt(l, {".mp3", ".wav", ".flac", ".aac", ".m4a"});
}

uint64_t storage::totalBytes() {
  return mounted ? SD_MMC.totalBytes() : 0;
}

uint64_t storage::usedBytes() {
  return mounted ? SD_MMC.usedBytes() : 0;
}

String storage::sanitizeName(const String &name) {
  String s;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == ' ' || c == '(' || c == ')')
      s += c;
    else
      s += '_';
  }
  s.trim();
  if (s.isEmpty()) s = "file";
  if (s.length() > 60) s = s.substring(s.length() - 60);
  return s;
}
