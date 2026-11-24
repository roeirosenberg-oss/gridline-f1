// Gridline F1 — firmware configuration
#pragma once

#define GL_FW_NAME "Gridline F1"
#define GL_FW_VERSION "1.2.0"
#define GL_USER_AGENT "GridlineF1/" GL_FW_VERSION " (ESP32; arduino-esp32 2.0.17)"

#ifndef GL_DISPLAY_PROFILE
#define GL_DISPLAY_PROFILE "ST7789"
#endif

// ---- Board: ESP32-2432S028 (classic ESP32, 4 MB flash, no PSRAM) ----
// Display (TFT_eSPI is configured via build flags in platformio.ini):
//   MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, RST -1, BL 21 (HSPI)
// Touch XPT2046 on its own SPI bus (VSPI):
#define GL_TOUCH_SCLK 25
#define GL_TOUCH_MISO 39
#define GL_TOUCH_MOSI 32
#define GL_TOUCH_CS 33
#define GL_TOUCH_IRQ 36
#define GL_PIN_BACKLIGHT 21
#define GL_PIN_BOOT_BUTTON 0
// On-board RGB LED (active low) – kept off.
#define GL_PIN_LED_R 4
#define GL_PIN_LED_G 16
#define GL_PIN_LED_B 17

#define GL_SCREEN_W 320
#define GL_SCREEN_H 240
// Default screen rotation (TFT_eSPI): 1 = landscape, 0 = portrait.
// The portrait build sets GL_DEFAULT_ROTATION=0. Settings -> Orientation
// overrides it at run time (no reflash needed).
#ifndef GL_DEFAULT_ROTATION
#define GL_DEFAULT_ROTATION 1
#endif

// ---- Network ----
#define GL_API_HOST "api.jolpi.ca"
#define GL_API_PORT 443
#define GL_API_BASE "/ergast/f1"
#define GL_HOSTNAME "gridline-f1"
#define GL_NTP_1 "pool.ntp.org"
#define GL_NTP_2 "time.google.com"
#define GL_NTP_3 "time.cloudflare.com"

#define GL_TCP_CONNECT_TIMEOUT_MS 10000
#define GL_TLS_HANDSHAKE_TIMEOUT_S 15
#define GL_HTTP_IDLE_TIMEOUT_MS 10000
#define GL_HTTP_TOTAL_TIMEOUT_MS 45000
#define GL_HTTP_MAX_BODY_BYTES (96 * 1024)
#define GL_MIN_REQUEST_SPACING_MS 2500  // API burst limit is 4 req/s; we stay far below
#define GL_MANUAL_REFRESH_COOLDOWN_S 60

// ---- Setup access point ----
#define GL_AP_IP_STR "192.168.4.1"
#define GL_AP_PREFIX "GRIDLINE-"

// ---- UI timing ----
#define GL_BOOT_HOLD_MS 3000      // hold BOOT this long to open Wi-Fi setup
#define GL_SETTINGS_SAVE_DELAY_MS 4000
