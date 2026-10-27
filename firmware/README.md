# Smart picture frame – firmware

Firmware for the Freenove FNK0086 ESP32-S3 Development Kit that turns it into a
Wi-Fi digital picture frame with a clock and a music player. Pictures and music
live on the microSD card and are uploaded from a phone over Wi-Fi.

## What it does (v0.1.0)

1. **First boot** – shows a *Connect to Wi-Fi* screen: scans networks, tap one,
   type the password on a large alphabetical keyboard (abc / ABC / 123 / #+=
   pages, ✓ connects, ← goes back). Credentials are stored, so later boots
   connect automatically. *Skip* runs the frame offline (clock can be set by hand).
2. **Clock / slideshow screen** – time and date (NTP via Wi-Fi, time zone
   selectable). With no pictures it shows the clock plus the frame's address and
   a QR code; with pictures it cross-fades through them with the clock in a band
   at the bottom.
3. **Phone web page** – scan the QR or open `http://<ip>` / `http://frame.local`:
   * *Pictures*: pick any images (JPEG, PNG, HEIC on iPhone, …). They are resized
     to the 240 × 320 screen **in the browser** (Fit or Fill) and uploaded as
     JPEG. Thumbnails, *Show*, *Delete*, *Prev/Next*.
   * *Music*: upload MP3/WAV/FLAC/AAC, play / pause / stop / next / prev, volume.
   * *Settings*: slideshow interval, shuffle, clock overlay, 24 h, time zone,
     "set time from this phone", repeat, device info.
4. **Touch menu** – tap the picture: Slideshow (interval, shuffle, clock
   overlay, prev/next), Clock (time zone, 24 h, manual time), Music (track list,
   transport, volume, repeat), Wi-Fi (status, change/forget network), About
   (address + QR, SD usage). Menus return to the pictures after 60 s idle.

## Build and flash

PlatformIO (`espressif32@6.13.0`, Arduino core 2.0.17). The board is on the
USB-UART (CH343) port; change `upload_port`/`monitor_port` in
[platformio.ini](platformio.ini) if it is not COM5.

```powershell
cd "smart picture frame\firmware"
pio run -t upload          # build + flash
pio device monitor         # 115200 baud serial log
```

Libraries: TFT_eSPI 2.5.43, LVGL 8.3.5 and ESP32-audioI2S 2.0.0 are vendored in
`lib/` (copied from the kit's `Libraries/`, docs/examples stripped); JPEGDEC is
pulled from the PlatformIO registry. The display setup is passed as build flags
(`USER_SETUP_LOADED`), LVGL is configured in [include/lv_conf.h](include/lv_conf.h).

> Note: the kit's `TFT_eSPI/User_Setup_Select.h` had a typo in its
> `USER_SETUP_LOADED` guard; the vendored copy is patched.

## Source layout

| File | Role |
| --- | --- |
| `src/main.cpp` | boot flow, main loop |
| `src/config.h` | pins, folders, constants |
| `src/settings.*` | persisted settings (NVS) and the time-zone table |
| `src/display.*` | TFT_eSPI + LVGL glue, FT6336U touch (direct I²C) |
| `src/storage.*` | SD_MMC mount, folder listing, name sanitising |
| `src/net.*` | Wi-Fi connect/scan state machine, mDNS, NTP, time zone |
| `src/slideshow.*` | JPEG decode (JPEGDEC → PSRAM frame buffers), ordering, timer |
| `src/player.*` | ESP32-audioI2S in its own task on core 0 |
| `src/web.*`, `src/web_page.h` | HTTP API + embedded phone UI |
| `src/ui.*` | all LVGL screens |

SD card layout: `/picture/*.jpg`, `/music/*.mp3|wav|flac|aac|m4a`.

## HTTP API

| Method / path | Purpose |
| --- | --- |
| `GET /` | phone UI |
| `GET /api/status` | JSON: time, settings, counts, now playing, SD/RAM |
| `GET /api/list?dir=picture\|music` | files |
| `GET /file?dir=&name=` | download (thumbnails) |
| `POST /upload?dir=&name=` (multipart `file`) | upload |
| `POST /api/delete?dir=&name=` | delete |
| `POST /api/show?name=` | show a picture |
| `POST /api/slideshow?cmd=next\|prev` | slideshow |
| `POST /api/music?cmd=play[&name=]\|pause\|stop\|next\|prev\|vol&value=` | music |
| `POST /api/settings?interval=&shuffle=&showClock=&clock24=&tz=&repeat=` | settings |
| `POST /api/time?epoch=` | set clock |
| `GET /api/timezones` | time-zone names |

## Ideas for next steps

* Landscape orientation option; auto-rotate landscape photos.
* Night mode / schedule (dim or blank the screen at night).
* Play music automatically with the slideshow; per-picture captions.
* Weather / calendar on the clock screen.
* OTA firmware update from the web page.
