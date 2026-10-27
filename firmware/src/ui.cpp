#include "ui.h"
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "slideshow.h"
#include "storage.h"

void openSub(int id);
void showPassword();

namespace {

enum class Screen { None, WifiList, WifiPassword, Connecting, Main, Menu, Slideshow, Clock, Music, Wifi, About };
Screen currentScreen = Screen::None;
bool idleReturn = false;   // return to Main after MENU_IDLE_TIMEOUT_MS without touch
bool skipAllowed = true;

// ---- main screen widgets ----
lv_obj_t *mainScr = nullptr;
lv_obj_t *imgA, *imgB;
lv_obj_t *imgTop = nullptr;  // the image currently fully visible
lv_obj_t *band, *bandTime, *bandDate;
lv_obj_t *emptyBox, *emptyTime, *emptyDate, *emptyHint, *emptyUrl, *emptyQr;
lv_obj_t *statusIcon;
bool firstImage = true;

// ---- music screen widgets (updated live) ----
lv_obj_t *musicNow = nullptr, *musicPlayBtnLabel = nullptr, *musicList = nullptr;

// ---- wifi ----
String pendingSsid;
lv_obj_t *passTa = nullptr;

const lv_color_t COL_BG = lv_color_hex(0x101418);
const lv_color_t COL_TEXT = lv_color_hex(0xF2F2F2);
const lv_color_t COL_DIM = lv_color_hex(0x9AA3AD);

void load(lv_obj_t *scr, Screen s, bool idle) {
  lv_obj_t *old = lv_scr_act();
  currentScreen = s;
  idleReturn = idle;
  if (old == scr) return;
  // Sub-screens are rebuilt on demand, so the previous one is freed; the main
  // screen is kept alive forever.
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, old != nullptr && old != mainScr);
}

lv_obj_t *makeScreen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_text_color(scr, COL_TEXT, 0);
  return scr;
}

// Screen with a header (back button + title) and a scrollable flex column body.
lv_obj_t *makeSubScreen(const char *title, lv_event_cb_t onBack, lv_obj_t **body) {
  lv_obj_t *scr = makeScreen();
  lv_obj_t *hdr = lv_obj_create(scr);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, SCREEN_W, 44);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1C2430), 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);

  lv_obj_t *back = lv_btn_create(hdr);
  lv_obj_set_size(back, 56, 36);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_t *bl = lv_label_create(back);
  lv_label_set_text(bl, LV_SYMBOL_LEFT);
  lv_obj_center(bl);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, NULL);

  lv_obj_t *t = lv_label_create(hdr);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
  lv_obj_align(t, LV_ALIGN_CENTER, 10, 0);

  lv_obj_t *b = lv_obj_create(scr);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, 0, 44);
  lv_obj_set_size(b, SCREEN_W, SCREEN_H - 44);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(b, 10, 0);
  lv_obj_set_style_pad_row(b, 10, 0);
  lv_obj_set_scroll_dir(b, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_AUTO);
  *body = b;
  return scr;
}

lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *font = nullptr, lv_color_t color = COL_TEXT) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  if (font) lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

lv_obj_t *button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *user = nullptr, int w = 200, int h = 44) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

// A labelled row: text on the left, a control (switch etc.) on the right.
lv_obj_t *row(lv_obj_t *parent, const char *txt) {
  lv_obj_t *r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, SCREEN_W - 20, 40);
  lv_obj_t *l = label(r, txt);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
  return r;
}

lv_obj_t *toggle(lv_obj_t *parent, const char *txt, bool on, lv_event_cb_t cb) {
  lv_obj_t *r = row(parent, txt);
  lv_obj_t *sw = lv_switch_create(r);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
  return sw;
}

// ---------------------------------------------------------------- clock text
void formatTime(char *out, size_t n, const tm &t) {
  if (settings.clock24) snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
  else {
    int h = t.tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(out, n, "%d:%02d", h, t.tm_min);
  }
}

void formatDate(char *out, size_t n, const tm &t) {
  static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  snprintf(out, n, "%s, %d %s %d", days[t.tm_wday], t.tm_mday, months[t.tm_mon], 1900 + t.tm_year);
}

void updateClock() {
  if (!mainScr) return;
  char tbuf[16], dbuf[48];
  if (net::timeValid()) {
    time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    formatTime(tbuf, sizeof tbuf, t);
    formatDate(dbuf, sizeof dbuf, t);
    if (!settings.clock24) {
      strncat(tbuf, t.tm_hour < 12 ? " am" : " pm", sizeof(tbuf) - strlen(tbuf) - 1);
    }
  } else {
    strcpy(tbuf, "--:--");
    strcpy(dbuf, net::state() == net::State::Connected ? "Waiting for time sync..." : "Time not set");
  }
  lv_label_set_text(bandTime, tbuf);
  lv_label_set_text(bandDate, dbuf);
  lv_label_set_text(emptyTime, tbuf);
  lv_label_set_text(emptyDate, dbuf);

  const char *icon = "";
  switch (net::state()) {
    case net::State::Connected: icon = ""; break;
    case net::State::Connecting: icon = LV_SYMBOL_REFRESH; break;
    default: icon = LV_SYMBOL_WARNING; break;
  }
  lv_label_set_text(statusIcon, icon);
}

void refreshEmptyState() {
  bool empty = slideshow::count() == 0;
  if (empty) {
    lv_obj_clear_flag(emptyBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(imgA, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(imgB, LV_OBJ_FLAG_HIDDEN);
    String url;
    if (net::state() == net::State::Connected) url = "http://" + net::ip();
    if (url.isEmpty()) {
      lv_label_set_text(emptyHint, "No pictures yet.\nConnect to Wi-Fi to upload\npictures from your phone.");
      lv_label_set_text(emptyUrl, "");
      lv_obj_add_flag(emptyQr, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(emptyHint, "No pictures yet.\nOpen this address on your phone\nto upload pictures and music:");
      lv_label_set_text(emptyUrl, url.c_str());
      lv_qrcode_update(emptyQr, url.c_str(), url.length());
      lv_obj_clear_flag(emptyQr, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_obj_add_flag(emptyBox, LV_OBJ_FLAG_HIDDEN);
    if (settings.showClock) lv_obj_clear_flag(band, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------- cross-fade
void fadeExec(void *obj, int32_t v) {
  lv_obj_set_style_img_opa((lv_obj_t *)obj, v, 0);
}

void fadeReady(lv_anim_t *a) {
  lv_obj_t *shown = (lv_obj_t *)a->var;
  lv_obj_t *other = shown == imgA ? imgB : imgA;
  lv_obj_add_flag(other, LV_OBJ_FLAG_HIDDEN);
}

void onImage(const lv_img_dsc_t *img, const String &) {
  if (!mainScr) return;
  lv_obj_t *target = (imgTop == imgA) ? imgB : imgA;
  lv_img_cache_invalidate_src(img);
  lv_img_set_src(target, img);
  lv_obj_clear_flag(target, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(target);
  lv_obj_move_foreground(band);
  lv_obj_move_foreground(statusIcon);
  if (firstImage) {
    lv_obj_set_style_img_opa(target, LV_OPA_COVER, 0);
    firstImage = false;
    if (imgTop) lv_obj_add_flag(imgTop, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_set_style_img_opa(target, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 700);
    lv_anim_set_exec_cb(&a, fadeExec);
    lv_anim_set_ready_cb(&a, fadeReady);
    lv_anim_start(&a);
  }
  imgTop = target;
  refreshEmptyState();
}

// ---------------------------------------------------------------- main screen
void mainClicked(lv_event_t *) {
  ui::showMenu();
}

void buildMain() {
  mainScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(mainScr, lv_color_black(), 0);
  lv_obj_set_style_text_color(mainScr, COL_TEXT, 0);
  lv_obj_clear_flag(mainScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(mainScr, mainClicked, LV_EVENT_CLICKED, NULL);

  imgA = lv_img_create(mainScr);
  imgB = lv_img_create(mainScr);
  for (lv_obj_t *i : {imgA, imgB}) {
    lv_obj_set_pos(i, 0, 0);
    lv_obj_set_size(i, SCREEN_W, SCREEN_H);
    lv_obj_add_flag(i, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
  }

  band = lv_obj_create(mainScr);
  lv_obj_remove_style_all(band);
  lv_obj_set_size(band, SCREEN_W, 86);
  lv_obj_align(band, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(band, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(band, LV_OPA_60, 0);
  lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);
  bandTime = label(band, "--:--", &lv_font_montserrat_48);
  lv_obj_align(bandTime, LV_ALIGN_TOP_MID, 0, 4);
  bandDate = label(band, "", &lv_font_montserrat_16, COL_DIM);
  lv_obj_align(bandDate, LV_ALIGN_BOTTOM_MID, 0, -8);

  emptyBox = lv_obj_create(mainScr);
  lv_obj_remove_style_all(emptyBox);
  lv_obj_set_size(emptyBox, SCREEN_W, SCREEN_H);
  lv_obj_clear_flag(emptyBox, LV_OBJ_FLAG_CLICKABLE);
  emptyTime = label(emptyBox, "--:--", &lv_font_montserrat_48);
  lv_obj_align(emptyTime, LV_ALIGN_TOP_MID, 0, 24);
  emptyDate = label(emptyBox, "", &lv_font_montserrat_16, COL_DIM);
  lv_obj_align(emptyDate, LV_ALIGN_TOP_MID, 0, 84);
  emptyHint = label(emptyBox, "", &lv_font_montserrat_14, COL_DIM);
  lv_obj_set_style_text_align(emptyHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(emptyHint, LV_ALIGN_TOP_MID, 0, 122);
  emptyUrl = label(emptyBox, "", &lv_font_montserrat_18, lv_palette_lighten(LV_PALETTE_BLUE, 2));
  lv_obj_align(emptyUrl, LV_ALIGN_TOP_MID, 0, 182);
  emptyQr = lv_qrcode_create(emptyBox, 96, lv_color_black(), lv_color_white());
  lv_obj_align(emptyQr, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_flag(emptyQr, LV_OBJ_FLAG_HIDDEN);

  statusIcon = label(mainScr, "", &lv_font_montserrat_14, COL_DIM);
  lv_obj_align(statusIcon, LV_ALIGN_TOP_RIGHT, -6, 6);
}

// ---------------------------------------------------------------- menu
void menuItem(lv_event_t *e) {
  openSub((int)(intptr_t)lv_event_get_user_data(e));
}

void goMenu(lv_event_t *) { ui::showMenu(); }
void goMain(lv_event_t *) { ui::showMain(); }

// ---------------------------------------------------------------- slideshow settings
const int INTERVALS[] = {5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
const char *INTERVAL_OPTS = "5 seconds\n10 seconds\n15 seconds\n30 seconds\n1 minute\n2 minutes\n5 minutes\n10 minutes\n30 minutes\n1 hour";

void intervalChanged(lv_event_t *e) {
  int i = lv_roller_get_selected(lv_event_get_target(e));
  settings.intervalSec = INTERVALS[i];
  settings.save();
  slideshow::resetTimer();
}
void shuffleChanged(lv_event_t *e) {
  settings.shuffle = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  settings.save();
  slideshow::refresh();
}
void showClockChanged(lv_event_t *e) {
  settings.showClock = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  settings.save();
  refreshEmptyState();
}
void nextPic(lv_event_t *) { slideshow::next(); }
void prevPic(lv_event_t *) { slideshow::prev(); }

void buildSlideshow() {
  lv_obj_t *body;
  lv_obj_t *scr = makeSubScreen("Slideshow", goMenu, &body);
  char buf[48];
  snprintf(buf, sizeof buf, "%d picture%s on the card", slideshow::count(), slideshow::count() == 1 ? "" : "s");
  label(body, buf, nullptr, COL_DIM);
  label(body, "Change picture every");
  lv_obj_t *r = lv_roller_create(body);
  lv_roller_set_options(r, INTERVAL_OPTS, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(r, 3);
  lv_obj_set_width(r, 180);
  int sel = 3;
  for (int i = 0; i < 10; i++)
    if (INTERVALS[i] == settings.intervalSec) sel = i;
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  lv_obj_add_event_cb(r, intervalChanged, LV_EVENT_VALUE_CHANGED, NULL);
  toggle(body, "Shuffle", settings.shuffle, shuffleChanged);
  toggle(body, "Show clock over pictures", settings.showClock, showClockChanged);
  lv_obj_t *rowBtns = lv_obj_create(body);
  lv_obj_remove_style_all(rowBtns);
  lv_obj_set_size(rowBtns, SCREEN_W - 20, 44);
  lv_obj_t *p = button(rowBtns, LV_SYMBOL_PREV " Prev", prevPic, nullptr, 100);
  lv_obj_align(p, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t *n = button(rowBtns, "Next " LV_SYMBOL_NEXT, nextPic, nullptr, 100);
  lv_obj_align(n, LV_ALIGN_RIGHT_MID, 0, 0);
  load(scr, Screen::Slideshow, true);
}

// ---------------------------------------------------------------- clock settings
lv_obj_t *hourRoller, *minRoller;

void tzChanged(lv_event_t *e) {
  settings.tzIndex = lv_roller_get_selected(lv_event_get_target(e));
  settings.save();
  net::applyTimezone();
  updateClock();
}
void clock24Changed(lv_event_t *e) {
  settings.clock24 = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  settings.save();
  updateClock();
}
void setManualTime(lv_event_t *) {
  time_t now = net::timeValid() ? time(nullptr) : 1735689600;  // 2025-01-01 if unknown
  tm t;
  localtime_r(&now, &t);
  t.tm_hour = lv_roller_get_selected(hourRoller);
  t.tm_min = lv_roller_get_selected(minRoller);
  t.tm_sec = 0;
  net::setTime(mktime(&t));
  updateClock();
  ui::toast("Time set");
}

void buildClock() {
  lv_obj_t *body;
  lv_obj_t *scr = makeSubScreen("Clock", goMenu, &body);
  label(body, "Time zone");
  String opts;
  for (int i = 0; i < TIMEZONE_COUNT; i++) {
    if (i) opts += "\n";
    opts += TIMEZONES[i].name;
  }
  lv_obj_t *r = lv_roller_create(body);
  lv_roller_set_options(r, opts.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(r, 3);
  lv_obj_set_width(r, 200);
  lv_roller_set_selected(r, settings.tzIndex, LV_ANIM_OFF);
  lv_obj_add_event_cb(r, tzChanged, LV_EVENT_VALUE_CHANGED, NULL);
  toggle(body, "24-hour clock", settings.clock24, clock24Changed);

  label(body, net::state() == net::State::Connected ? "Time is synced from the internet.\nYou can still set it manually:" : "Set the time manually:",
        &lv_font_montserrat_14, COL_DIM);
  lv_obj_t *tr = lv_obj_create(body);
  lv_obj_remove_style_all(tr);
  lv_obj_set_size(tr, SCREEN_W - 20, 110);
  String hours, mins;
  for (int i = 0; i < 24; i++) hours += (i ? "\n" : "") + String(i < 10 ? "0" : "") + String(i);
  for (int i = 0; i < 60; i++) mins += (i ? "\n" : "") + String(i < 10 ? "0" : "") + String(i);
  time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  hourRoller = lv_roller_create(tr);
  lv_roller_set_options(hourRoller, hours.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(hourRoller, 3);
  lv_obj_set_width(hourRoller, 60);
  lv_roller_set_selected(hourRoller, t.tm_hour, LV_ANIM_OFF);
  lv_obj_align(hourRoller, LV_ALIGN_LEFT_MID, 20, 0);
  lv_obj_t *colon = label(tr, ":", &lv_font_montserrat_24);
  lv_obj_align(colon, LV_ALIGN_LEFT_MID, 88, 0);
  minRoller = lv_roller_create(tr);
  lv_roller_set_options(minRoller, mins.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(minRoller, 3);
  lv_obj_set_width(minRoller, 60);
  lv_roller_set_selected(minRoller, t.tm_min, LV_ANIM_OFF);
  lv_obj_align(minRoller, LV_ALIGN_LEFT_MID, 100, 0);
  lv_obj_t *set = button(tr, "Set", setManualTime, nullptr, 50, 44);
  lv_obj_align(set, LV_ALIGN_RIGHT_MID, 0, 0);
  load(scr, Screen::Clock, true);
}

// ---------------------------------------------------------------- music
void refreshMusicWidgets() {
  if (currentScreen != Screen::Music || !musicNow) return;
  String n = player::currentName();
  lv_label_set_text(musicNow, n.isEmpty() ? "Nothing playing" : n.c_str());
  lv_label_set_text(musicPlayBtnLabel, (player::playing() && !player::isPaused()) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  int cur = player::currentIndex();
  uint32_t cnt = lv_obj_get_child_cnt(musicList);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t *b = lv_obj_get_child(musicList, i);
    if ((int)i == cur) lv_obj_add_state(b, LV_STATE_CHECKED);
    else lv_obj_clear_state(b, LV_STATE_CHECKED);
  }
}

void musicTrack(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!player::playIndex(i)) ui::toast("Cannot play this file");
  refreshMusicWidgets();
}
void musicPlayPause(lv_event_t *) {
  if (player::playing()) player::pauseResume();
  else if (!player::files().empty()) player::playIndex(player::currentIndex() < 0 ? 0 : player::currentIndex());
  refreshMusicWidgets();
}
void musicStop(lv_event_t *) { player::stop(); refreshMusicWidgets(); }
void musicNext(lv_event_t *) { player::next(); refreshMusicWidgets(); }
void musicPrev(lv_event_t *) { player::prev(); refreshMusicWidgets(); }
void volChanged(lv_event_t *e) {
  player::setVolume(lv_slider_get_value(lv_event_get_target(e)));
}
void volReleased(lv_event_t *) { settings.save(); }
void repeatChanged(lv_event_t *e) {
  settings.musicRepeat = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  settings.save();
}

void buildMusic() {
  lv_obj_t *body;
  lv_obj_t *scr = makeSubScreen("Music", goMenu, &body);
  lv_obj_set_style_pad_row(body, 6, 0);
  musicNow = label(body, "Nothing playing", &lv_font_montserrat_16);
  lv_label_set_long_mode(musicNow, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(musicNow, SCREEN_W - 20);
  lv_obj_set_style_text_align(musicNow, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *ctl = lv_obj_create(body);
  lv_obj_remove_style_all(ctl);
  lv_obj_set_size(ctl, SCREEN_W - 20, 48);
  lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctl, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  button(ctl, LV_SYMBOL_PREV, musicPrev, nullptr, 48, 44);
  lv_obj_t *pp = button(ctl, LV_SYMBOL_PLAY, musicPlayPause, nullptr, 56, 44);
  musicPlayBtnLabel = lv_obj_get_child(pp, 0);
  button(ctl, LV_SYMBOL_STOP, musicStop, nullptr, 48, 44);
  button(ctl, LV_SYMBOL_NEXT, musicNext, nullptr, 48, 44);

  lv_obj_t *vr = lv_obj_create(body);
  lv_obj_remove_style_all(vr);
  lv_obj_set_size(vr, SCREEN_W - 20, 36);
  lv_obj_t *vi = label(vr, LV_SYMBOL_VOLUME_MAX, nullptr, COL_DIM);
  lv_obj_align(vi, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t *sl = lv_slider_create(vr);
  lv_slider_set_range(sl, 0, 21);
  lv_slider_set_value(sl, player::volume(), LV_ANIM_OFF);
  lv_obj_set_width(sl, SCREEN_W - 70);
  lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_event_cb(sl, volChanged, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(sl, volReleased, LV_EVENT_RELEASED, NULL);

  toggle(body, "Repeat all", settings.musicRepeat, repeatChanged);

  musicList = lv_list_create(body);
  lv_obj_set_size(musicList, SCREEN_W - 20, 110);
  lv_obj_set_flex_grow(musicList, 1);
  const auto &files = player::files();
  if (files.empty()) lv_list_add_text(musicList, "No music on the card yet");
  for (size_t i = 0; i < files.size(); i++) {
    lv_obj_t *b = lv_list_add_btn(musicList, LV_SYMBOL_AUDIO, files[i].c_str());
    lv_obj_add_event_cb(b, musicTrack, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  load(scr, Screen::Music, true);
  refreshMusicWidgets();
}

// ---------------------------------------------------------------- wifi status
void changeNetwork(lv_event_t *) { ui::showWifiSetup(true); }
void forgetNetwork(lv_event_t *) {
  settings.wifiSsid = "";
  settings.wifiPass = "";
  settings.save();
  net::disconnect();
  ui::showWifiSetup(true);
}

void buildWifi() {
  lv_obj_t *body;
  lv_obj_t *scr = makeSubScreen("Wi-Fi", goMenu, &body);
  String s;
  switch (net::state()) {
    case net::State::Connected:
      s = "Connected to " + net::ssid() + "\nIP address: " + net::ip() + "\nSignal: " + String(net::rssi()) + " dBm\nAlso reachable as\nhttp://" DEVICE_HOSTNAME ".local";
      break;
    case net::State::Connecting: s = "Connecting to " + settings.wifiSsid + "..."; break;
    case net::State::Offline: s = "Wi-Fi is off (skipped)."; break;
    default: s = settings.wifiSsid.isEmpty() ? "No network configured." : "Not connected to " + settings.wifiSsid; break;
  }
  lv_obj_t *l = label(body, s.c_str());
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, SCREEN_W - 20);
  button(body, "Choose another network", changeNetwork);
  if (!settings.wifiSsid.isEmpty()) button(body, "Forget this network", forgetNetwork);
  load(scr, Screen::Wifi, true);
}

// ---------------------------------------------------------------- about
void buildAbout() {
  lv_obj_t *body;
  lv_obj_t *scr = makeSubScreen("About", goMenu, &body);
  label(body, "Smart Picture Frame v" FW_VERSION, &lv_font_montserrat_16);
  String url = net::state() == net::State::Connected ? "http://" + net::ip() : "";
  if (!url.isEmpty()) {
    label(body, "Open on your phone:", nullptr, COL_DIM);
    label(body, url.c_str(), &lv_font_montserrat_16, lv_palette_lighten(LV_PALETTE_BLUE, 2));
    lv_obj_t *qr = lv_qrcode_create(body, 110, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, url.c_str(), url.length());
  } else {
    label(body, "Not connected to Wi-Fi", nullptr, COL_DIM);
  }
  char buf[96];
  snprintf(buf, sizeof buf, "SD card: %llu / %llu MB used\n%d pictures, %d songs", storage::usedBytes() >> 20,
           storage::totalBytes() >> 20, slideshow::count(), (int)player::files().size());
  lv_obj_t *l = label(body, buf, nullptr, COL_DIM);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  snprintf(buf, sizeof buf, "Free RAM %u KB, PSRAM %u KB", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
  label(body, buf, nullptr, COL_DIM);
  load(scr, Screen::About, true);
}

// ---------------------------------------------------------------- wifi setup
void wifiScanTimer(lv_timer_t *t);
lv_obj_t *wifiList = nullptr, *wifiSpinner = nullptr;
lv_timer_t *scanTimer = nullptr;

void pickNetwork(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  pendingSsid = lv_list_get_btn_text(wifiList, btn);
  showPassword();
}
void rescan(lv_event_t *) {
  if (!wifiList) return;
  lv_obj_clean(wifiList);
  lv_obj_clear_flag(wifiSpinner, LV_OBJ_FLAG_HIDDEN);
  net::startScan();
  if (!scanTimer) scanTimer = lv_timer_create(wifiScanTimer, 300, NULL);
}
void skipWifi(lv_event_t *) {
  net::setOffline();
  ui::showMain();
}

void wifiScanTimer(lv_timer_t *t) {
  if (currentScreen != Screen::WifiList || !wifiList) {
    lv_timer_del(t);
    scanTimer = nullptr;
    return;
  }
  if (!net::scanFinished()) return;
  lv_timer_del(t);
  scanTimer = nullptr;
  lv_obj_add_flag(wifiSpinner, LV_OBJ_FLAG_HIDDEN);
  auto res = net::scanResults();
  if (res.empty()) lv_list_add_text(wifiList, "No networks found");
  for (auto &r : res) {
    const char *icon = r.rssi > -60 ? LV_SYMBOL_WIFI : (r.rssi > -75 ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING);
    lv_obj_t *b = lv_list_add_btn(wifiList, icon, r.ssid.c_str());
    lv_obj_add_event_cb(b, pickNetwork, LV_EVENT_CLICKED, NULL);
  }
}

void passReady(lv_event_t *) {
  String pass = lv_textarea_get_text(passTa);
  settings.wifiSsid = pendingSsid;
  settings.wifiPass = pass;
  settings.save();
  ui::showConnecting(pendingSsid);
  net::connect(pendingSsid, pass);
}
void passCancel(lv_event_t *) { ui::showWifiSetup(skipAllowed); }

// Large-key keyboard: 7 alphabetical keys per row (~34 px wide, 47 px tall)
// instead of LVGL's default 10-key QWERTY rows, which are hard to hit on a
// 2.8" screen. Letters are alphabetical so they are easy to find.
#define KB_W(n) ((lv_btnmatrix_ctrl_t)(n))
#define KB_CTL(n) ((lv_btnmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BTN_FLAGS | (n)))
const char *KB_LOWER[] = {"a", "b", "c", "d", "e", "f", "g", "\n",
                                "h", "i", "j", "k", "l", "m", "n", "\n",
                                "o", "p", "q", "r", "s", "t", "u", "\n",
                                "v", "w", "x", "y", "z", LV_SYMBOL_BACKSPACE, "\n",
                                "ABC", "123", " ", LV_SYMBOL_OK, ""};
const char *KB_UPPER[] = {"A", "B", "C", "D", "E", "F", "G", "\n",
                                "H", "I", "J", "K", "L", "M", "N", "\n",
                                "O", "P", "Q", "R", "S", "T", "U", "\n",
                                "V", "W", "X", "Y", "Z", LV_SYMBOL_BACKSPACE, "\n",
                                "abc", "123", " ", LV_SYMBOL_OK, ""};
const char *KB_NUMBER[] = {"1", "2", "3", "4", "5", "6", "7", "\n",
                                 "8", "9", "0", ".", ",", "-", "_", "\n",
                                 "@", "#", "$", "%", "&", "*", "+", "\n",
                                 "!", "?", "/", "(", ")", LV_SYMBOL_BACKSPACE, "\n",
                                 "abc", "#+=", " ", LV_SYMBOL_OK, ""};
const char *KB_SPECIAL[] = {":", ";", "'", "\"", "<", ">", "=", "\n",
                                  "[", "]", "{", "}", "^", "`", "|", "\n",
                                  "\\", "~", LV_SYMBOL_BACKSPACE, "\n",
                                  "abc", "123", " ", LV_SYMBOL_OK, ""};
const lv_btnmatrix_ctrl_t KB_CTRL_LETTERS[] = {
  KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2),
  KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2),
  KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2),
  KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_CTL(4),
  KB_CTL(3), KB_CTL(3), KB_W(4), KB_CTL(4)};
const lv_btnmatrix_ctrl_t KB_CTRL_SPECIAL[] = {
  KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2),
  KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2), KB_W(2),
  KB_W(2), KB_W(2), KB_CTL(10),
  KB_CTL(3), KB_CTL(3), KB_W(4), KB_CTL(4)};

// Handles our extra page-switch keys, then defers to LVGL's default handler
// (letters, backspace, OK, abc/ABC).
void kbEvent(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  const char *txt = id == LV_BTNMATRIX_BTN_NONE ? nullptr : lv_btnmatrix_get_btn_text(kb, id);
  if (txt && strcmp(txt, "123") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER); return; }
  if (txt && strcmp(txt, "#+=") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL); return; }
  lv_keyboard_def_event_cb(e);
}

void retryConnect(lv_event_t *) {
  ui::showConnecting(settings.wifiSsid);
  net::connect(settings.wifiSsid, settings.wifiPass);
}
void chooseNetwork(lv_event_t *) { ui::showWifiSetup(skipAllowed); }

}  // namespace

void showPassword() {
  lv_obj_t *scr = makeScreen();
  lv_obj_t *back = lv_btn_create(scr);
  lv_obj_set_size(back, 44, 36);
  lv_obj_set_pos(back, 4, 4);
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_t *bl = lv_label_create(back);
  lv_label_set_text(bl, LV_SYMBOL_LEFT);
  lv_obj_center(bl);
  lv_obj_add_event_cb(back, passCancel, LV_EVENT_CLICKED, NULL);

  String t = "Password for " + pendingSsid;
  lv_obj_t *title = label(scr, t.c_str(), &lv_font_montserrat_14);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, SCREEN_W - 56);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 50, 14);

  passTa = lv_textarea_create(scr);
  lv_textarea_set_one_line(passTa, true);
  lv_textarea_set_password_mode(passTa, true);
  lv_textarea_set_password_show_time(passTa, 2000);
  lv_textarea_set_placeholder_text(passTa, "Wi-Fi password, then " LV_SYMBOL_OK);
  lv_obj_set_style_text_font(passTa, &lv_font_montserrat_16, 0);
  lv_obj_set_width(passTa, SCREEN_W - 8);
  lv_obj_align(passTa, LV_ALIGN_TOP_MID, 0, 44);

  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL_LETTERS);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL_LETTERS);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_NUMBER, KB_NUMBER, KB_CTRL_LETTERS);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SPECIAL, KB_CTRL_SPECIAL);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_popovers(kb, true);  // show the pressed key above the finger
  lv_obj_set_size(kb, SCREEN_W, 232);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_all(kb, 3, 0);
  lv_obj_set_style_pad_gap(kb, 3, 0);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_18, LV_PART_ITEMS);
  lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(kb, kbEvent, LV_EVENT_VALUE_CHANGED, NULL);
  lv_keyboard_set_textarea(kb, passTa);
  lv_obj_add_event_cb(kb, passReady, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, passCancel, LV_EVENT_CANCEL, NULL);
  load(scr, Screen::WifiPassword, false);
}

void openSub(int id) {
  switch (id) {
    case 0: buildSlideshow(); break;
    case 1: buildClock(); break;
    case 2: buildMusic(); break;
    case 3: buildWifi(); break;
    case 4: buildAbout(); break;
    default: ui::showMain(); break;
  }
}

// ================================================================ public API
void ui::init() {
  buildMain();
  slideshow::onImage(onImage);
  lv_timer_create([](lv_timer_t *) { updateClock(); }, 1000, NULL);
  refreshEmptyState();
  updateClock();
}

void ui::showMain() {
  musicNow = nullptr;
  musicList = nullptr;
  wifiList = nullptr;
  refreshEmptyState();
  updateClock();
  load(mainScr, Screen::Main, false);
  slideshow::resetTimer();
}

bool ui::mainVisible() {
  return currentScreen == Screen::Main;
}

void ui::showMenu() {
  lv_obj_t *body;
  lv_obj_t *scr = makeSubScreen("Menu", goMain, &body);
  lv_obj_set_style_pad_row(body, 8, 0);
  struct { const char *icon; const char *txt; } items[] = {
    {LV_SYMBOL_IMAGE, "Slideshow"}, {LV_SYMBOL_BELL, "Clock"}, {LV_SYMBOL_AUDIO, "Music"},
    {LV_SYMBOL_WIFI, "Wi-Fi"}, {LV_SYMBOL_SETTINGS, "About"}, {LV_SYMBOL_LEFT, "Back to pictures"},
  };
  for (int i = 0; i < 6; i++) {
    String t = String(items[i].icon) + "   " + items[i].txt;
    lv_obj_t *b = button(body, t.c_str(), menuItem, (void *)(intptr_t)i, SCREEN_W - 20, 40);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x263241), 0);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
  }
  load(scr, Screen::Menu, true);
}

void ui::showWifiSetup(bool allowSkip) {
  skipAllowed = allowSkip;
  lv_obj_t *scr = makeScreen();
  lv_obj_t *t = label(scr, "Connect to Wi-Fi", &lv_font_montserrat_18);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
  wifiSpinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(wifiSpinner, 28, 28);
  lv_obj_align(wifiSpinner, LV_ALIGN_TOP_RIGHT, -10, 6);

  wifiList = lv_list_create(scr);
  lv_obj_set_size(wifiList, SCREEN_W - 16, SCREEN_H - 44 - 56);
  lv_obj_align(wifiList, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, SCREEN_W, 52);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  button(bar, LV_SYMBOL_REFRESH " Rescan", rescan, nullptr, 104, 40);
  if (net::state() == net::State::Connected) button(bar, LV_SYMBOL_LEFT " Back", goMain, nullptr, 104, 40);
  else if (allowSkip) button(bar, "Skip " LV_SYMBOL_RIGHT, skipWifi, nullptr, 104, 40);

  load(scr, Screen::WifiList, false);
  rescan(nullptr);
}

void ui::showConnecting(const String &ssid) {
  lv_obj_t *scr = makeScreen();
  lv_obj_t *sp = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(sp, 70, 70);
  lv_obj_align(sp, LV_ALIGN_CENTER, 0, -40);
  String t = "Connecting to\n" + ssid + "...";
  lv_obj_t *l = label(scr, t.c_str(), &lv_font_montserrat_16);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 30);
  load(scr, Screen::Connecting, false);
}

void ui::showConnectFailed(const String &ssid) {
  lv_obj_t *scr = makeScreen();
  lv_obj_t *icon = label(scr, LV_SYMBOL_WARNING, &lv_font_montserrat_48, lv_palette_main(LV_PALETTE_AMBER));
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 30);
  String t = "Could not connect to\n" + ssid;
  lv_obj_t *l = label(scr, t.c_str(), &lv_font_montserrat_16);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 100);
  lv_obj_t *col = lv_obj_create(scr);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, SCREEN_W, 160);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 8, 0);
  button(col, "Try again", retryConnect);
  button(col, "Choose another network", chooseNetwork);
  button(col, "Continue without Wi-Fi", skipWifi);
  load(scr, Screen::Connecting, false);
}

void ui::toast(const String &msg, uint32_t ms) {
  lv_obj_t *box = lv_obj_create(lv_layer_top());
  lv_obj_set_style_bg_color(box, lv_color_hex(0x2A3442), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_90, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_radius(box, 10, 0);
  lv_obj_set_style_pad_all(box, 10, 0);
  lv_obj_set_width(box, SCREEN_W - 30);
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -100);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *l = label(box, msg.c_str());
  lv_obj_set_width(l, lv_pct(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_timer_t *t = lv_timer_create([](lv_timer_t *t) { lv_obj_del((lv_obj_t *)t->user_data); }, ms, box);
  lv_timer_set_repeat_count(t, 1);
}

void ui::picturesChanged() {
  slideshow::refresh();
  refreshEmptyState();
  if (slideshow::count() > 0 && slideshow::currentName().isEmpty()) slideshow::next();
}

void ui::musicChanged() {
  player::refresh();
  if (currentScreen == Screen::Music) buildMusic();
}

void ui::settingsChanged() {
  net::applyTimezone();
  refreshEmptyState();
  updateClock();
}

void ui::loop() {
  static uint32_t lastMusicRefresh = 0;
  if (idleReturn && millis() - display::lastTouchMillis() > MENU_IDLE_TIMEOUT_MS) ui::showMain();
  if (currentScreen == Screen::Music && millis() - lastMusicRefresh > 1000) {
    lastMusicRefresh = millis();
    refreshMusicWidgets();
  }
  // Pictures only advance while the slideshow is on screen.
  slideshow::setPaused(currentScreen != Screen::Main);
}
