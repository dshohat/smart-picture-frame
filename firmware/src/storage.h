#pragma once
#include <Arduino.h>
#include <vector>

namespace storage {
bool init();
bool ready();
// Returns sorted file names (no directory part) in `dir` matching one of the
// lower-case extensions (e.g. {".jpg", ".jpeg"}).
std::vector<String> list(const char *dir, std::initializer_list<const char *> exts);
bool remove(const String &path);
bool isPicture(const String &name);
bool isMusic(const String &name);
uint64_t totalBytes();
uint64_t usedBytes();
// Replace unsafe characters so the name is a plain file name.
String sanitizeName(const String &name);
}
