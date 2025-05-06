#ifndef DISPLAY_H
#define DISPLAY_H

#include "EpdSpi.h"
#include "Gdew075T7.h"
// #include "gdem029E97.h"

#include <Fonts/Roboto_Condensed_SemiBold40pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold60pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold75pt7b.h>

/* WiFi credentials configured via menuconfig */
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD

#define Y_OFFSET 40
#define LINE_SPACING 50

// Tell every file “io” and “display” are defined elsewhere:
extern EpdSpi io;
extern Gdew075T7 display;

void display_start_screen(void);

uint16_t printCenteredLine(const char *text, uint16_t startY, uint16_t availWidth);

const GFXfont *selectFontForText(const char *text, uint16_t availWidth, uint16_t availHeight);

#endif