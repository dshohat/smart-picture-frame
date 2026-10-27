#pragma once
#include <Arduino.h>
#include <vector>

namespace player {
void init();
void loop();     // main-loop housekeeping (track advance, callbacks)
void refresh();  // rescan the music folder
const std::vector<String> &files();

bool play(const String &name);
bool playIndex(int index);
void stop();
void pauseResume();
void next();
void prev();
void setVolume(int v);  // 0..21
int volume();

bool playing();  // a track is loaded (may be paused)
bool isPaused();
String currentName();
int currentIndex();
}
