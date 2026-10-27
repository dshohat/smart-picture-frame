#include "slideshow.h"
#include <FS.h>
#include <SD_MMC.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "settings.h"
#include "storage.h"

// Two full-screen RGB565 frame buffers in PSRAM; pictures are decoded into the
// one not currently shown so the UI can cross-fade between them.
static lv_img_dsc_t frames[2];
static int backFrame = 0;

static JPEGDEC *jpeg;
static std::vector<String> names;
static std::vector<uint16_t> order;  // shuffled indices
static int pos = -1;                 // position in `order`
static String current;
static bool isPaused = false;
static uint32_t lastChange = 0;
static slideshow::ImageCallback imageCb = nullptr;

struct DecodeCtx {
  uint16_t *dst;
  int offX, offY;    // placement of the scaled image inside the frame
  int outW, outH;    // scaled output size as produced by JPEGDEC
  int fitW, fitH;    // final size after our own extra downsampling
};

static int drawCb(JPEGDRAW *d) {
  DecodeCtx *c = (DecodeCtx *)d->pUser;
  bool extra = (c->fitW != c->outW) || (c->fitH != c->outH);
  for (int y = 0; y < d->iHeight; y++) {
    int sy = d->y + y;
    int dy = extra ? (int)((int64_t)sy * c->fitH / c->outH) : sy;
    dy += c->offY;
    if (dy < 0 || dy >= SCREEN_H) continue;
    uint16_t *row = c->dst + dy * SCREEN_W;
    uint16_t *src = d->pPixels + y * d->iWidth;
    for (int x = 0; x < d->iWidth; x++) {
      int sx = d->x + x;
      int dx = extra ? (int)((int64_t)sx * c->fitW / c->outW) : sx;
      dx += c->offX;
      if (dx < 0 || dx >= SCREEN_W) continue;
      row[dx] = src[x];
    }
  }
  return 1;
}

// File callbacks for JPEGDEC (decoding straight from the SD card).
static void *jOpen(const char *path, int32_t *size) {
  File *f = new File(SD_MMC.open(path, FILE_READ));
  if (!*f) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}
static void jClose(void *h) {
  File *f = (File *)h;
  if (f) {
    f->close();
    delete f;
  }
}
static int32_t jRead(JPEGFILE *pf, uint8_t *buf, int32_t len) {
  return ((File *)pf->fHandle)->read(buf, len);
}
static int32_t jSeek(JPEGFILE *pf, int32_t pos) {
  return ((File *)pf->fHandle)->seek(pos) ? pos : -1;
}

static bool decodeInto(const String &name, uint16_t *dst) {
  String path = String(PICTURE_DIR) + "/" + name;
  if (!jpeg->open(path.c_str(), jOpen, jClose, jRead, jSeek, drawCb)) {
    Serial.printf("[pic] cannot decode %s (err %d)\n", name.c_str(), jpeg->getLastError());
    return false;
  }
  int w = jpeg->getWidth(), h = jpeg->getHeight();
  int scale = 1, opt = 0;
  if (w > SCREEN_W * 4 || h > SCREEN_H * 4) { scale = 8; opt = JPEG_SCALE_EIGHTH; }
  else if (w > SCREEN_W * 2 || h > SCREEN_H * 2) { scale = 4; opt = JPEG_SCALE_QUARTER; }
  else if (w > SCREEN_W || h > SCREEN_H) { scale = 2; opt = JPEG_SCALE_HALF; }

  DecodeCtx c;
  c.dst = dst;
  c.outW = (w + scale - 1) / scale;
  c.outH = (h + scale - 1) / scale;
  // Fit (letterbox) whatever remains after the hardware-ish scaling.
  float k = min((float)SCREEN_W / c.outW, (float)SCREEN_H / c.outH);
  if (k > 1.0f) k = 1.0f;
  c.fitW = max(1, (int)(c.outW * k));
  c.fitH = max(1, (int)(c.outH * k));
  c.offX = (SCREEN_W - c.fitW) / 2;
  c.offY = (SCREEN_H - c.fitH) / 2;

  memset(dst, 0, SCREEN_W * SCREEN_H * 2);
  jpeg->setUserPointer(&c);
  jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
  uint32_t t0 = millis();
  int ok = jpeg->decode(0, 0, opt);
  jpeg->close();
  Serial.printf("[pic] %s %dx%d scale 1/%d -> %dx%d in %lu ms %s\n", name.c_str(), w, h, scale, c.fitW, c.fitH,
                millis() - t0, ok ? "" : "(decode error)");
  return ok != 0;
}

static void rebuildOrder() {
  order.clear();
  for (size_t i = 0; i < names.size(); i++) order.push_back(i);
  if (settings.shuffle) {
    for (int i = (int)order.size() - 1; i > 0; i--) {
      int j = esp_random() % (i + 1);
      std::swap(order[i], order[j]);
    }
  }
}

void slideshow::init() {
  for (int i = 0; i < 2; i++) {
    frames[i].header.always_zero = 0;
    frames[i].header.w = SCREEN_W;
    frames[i].header.h = SCREEN_H;
    frames[i].header.cf = LV_IMG_CF_TRUE_COLOR;
    frames[i].data_size = SCREEN_W * SCREEN_H * 2;
    frames[i].data = (const uint8_t *)heap_caps_calloc(SCREEN_W * SCREEN_H, 2, MALLOC_CAP_SPIRAM);
  }
  void *mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
  jpeg = new (mem) JPEGDEC();
  refresh();
}

void slideshow::refresh() {
  names = storage::list(PICTURE_DIR, {".jpg", ".jpeg"});
  rebuildOrder();
  // keep position on the current picture if it still exists
  pos = -1;
  for (size_t i = 0; i < order.size(); i++)
    if (names[order[i]] == current) pos = i;
  if (pos < 0) current = "";
}

int slideshow::count() {
  return names.size();
}

const std::vector<String> &slideshow::files() {
  return names;
}

String slideshow::currentName() {
  return current;
}

int slideshow::currentIndex() {
  return pos >= 0 && pos < (int)order.size() ? order[pos] : -1;
}

static bool showAt(int p) {
  if (names.empty()) return false;
  p = ((p % (int)order.size()) + order.size()) % order.size();
  const String &name = names[order[p]];
  if (!decodeInto(name, (uint16_t *)frames[backFrame].data)) return false;
  pos = p;
  current = name;
  lastChange = millis();
  if (imageCb) imageCb(&frames[backFrame], name);
  backFrame ^= 1;
  return true;
}

bool slideshow::next() {
  // Skip over undecodable files, but never loop forever.
  for (size_t tries = 0; tries < names.size(); tries++) {
    if (showAt(pos + 1 + tries)) return true;
  }
  return false;
}

bool slideshow::prev() {
  for (size_t tries = 0; tries < names.size(); tries++) {
    if (showAt(pos - 1 - tries)) return true;
  }
  return false;
}

bool slideshow::show(const String &name) {
  for (size_t i = 0; i < order.size(); i++)
    if (names[order[i]] == name) return showAt(i);
  return false;
}

bool slideshow::showIndex(int index) {
  for (size_t i = 0; i < order.size(); i++)
    if (order[i] == index) return showAt(i);
  return false;
}

void slideshow::setPaused(bool p) {
  if (isPaused && !p) lastChange = millis();  // resume: give the current picture a full interval
  isPaused = p;
}

bool slideshow::paused() {
  return isPaused;
}

void slideshow::resetTimer() {
  lastChange = millis();
}

void slideshow::onImage(ImageCallback cb) {
  imageCb = cb;
}

void slideshow::loop() {
  if (names.empty() || isPaused) return;
  if (current.isEmpty()) {
    // Nothing on screen yet (or the shown file was deleted): load one, but do
    // not hammer the card if every file fails to decode.
    if (lastChange == 0 || millis() - lastChange > 3000) {
      if (!next()) lastChange = millis();
    }
    return;
  }
  if (names.size() < 2) return;
  if (millis() - lastChange >= (uint32_t)settings.intervalSec * 1000UL) next();
}
