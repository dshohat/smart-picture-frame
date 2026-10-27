#pragma once

#define FW_VERSION "0.1.0"
#define DEVICE_HOSTNAME "frame"   // reachable as http://frame.local

// Display
#define SCREEN_W 240
#define SCREEN_H 320

// Touch (FT6336U over I2C)
#define TOUCH_SDA 2
#define TOUCH_SCL 1
#define TOUCH_I2C_ADDR 0x38

// microSD (SD_MMC, 1-bit)
#define SD_MMC_CMD 38
#define SD_MMC_CLK 39
#define SD_MMC_D0 40

// I2S amplifier
#define I2S_BCLK 42
#define I2S_DOUT 41
#define I2S_LRC 14

// SD card folders
#define PICTURE_DIR "/picture"
#define MUSIC_DIR "/music"

// Behaviour
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define MENU_IDLE_TIMEOUT_MS 60000
#define MAX_FILES 400
#define MAX_UPLOAD_BYTES (20UL * 1024UL * 1024UL)
