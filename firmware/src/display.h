#pragma once
#include <lvgl.h>

namespace display {
void init();
void loop();                 // call frequently from the main loop
uint32_t lastTouchMillis();  // for idle timeouts
}
