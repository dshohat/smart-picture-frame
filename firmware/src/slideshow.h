#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <vector>

namespace slideshow {
typedef void (*ImageCallback)(const lv_img_dsc_t *img, const String &name);

void init();
void loop();
void refresh();  // rescan the picture folder
int count();
const std::vector<String> &files();
String currentName();
int currentIndex();

bool next();
bool prev();
bool show(const String &name);
bool showIndex(int index);

void setPaused(bool paused);
bool paused();
void resetTimer();
void onImage(ImageCallback cb);  // called after a picture was decoded
}
