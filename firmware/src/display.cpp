#include "display.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "config.h"

static TFT_eSPI tft(SCREEN_W, SCREEN_H);
static lv_disp_draw_buf_t drawBuf;
static lv_color_t *buf1;
static lv_color_t *buf2;
static const uint32_t BUF_PIXELS = SCREEN_W * 40;
static uint32_t lastTouch = 0;

static void flushCb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// Minimal FT6336U reader: one 7-byte burst for status + first touch point.
static bool readTouch(uint16_t &x, uint16_t &y) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5) != 5) return false;
  uint8_t d[5];
  for (int i = 0; i < 5; i++) d[i] = Wire.read();
  uint8_t points = d[0] & 0x0F;
  if (points == 0 || points > 2) return false;
  x = ((d[1] & 0x0F) << 8) | d[2];
  y = ((d[3] & 0x0F) << 8) | d[4];
  return x < SCREEN_W && y < SCREEN_H;
}

static void touchCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint16_t x, y;
  if (readTouch(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    lastTouch = millis();
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void display::init() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  lv_init();
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  buf1 = (lv_color_t *)heap_caps_malloc(BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  buf2 = (lv_color_t *)heap_caps_malloc(BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, BUF_PIXELS);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_t *disp = lv_disp_drv_register(&dispDrv);

  lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                            lv_palette_main(LV_PALETTE_AMBER), true,
                                            &lv_font_montserrat_14);
  lv_disp_set_theme(disp, theme);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);
  lastTouch = millis();
}

void display::loop() {
  lv_timer_handler();
}

uint32_t display::lastTouchMillis() {
  return lastTouch;
}
