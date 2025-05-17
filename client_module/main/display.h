#ifndef DISPLAY_H
#define DISPLAY_H

#define GDEW_075T7
// #define GDEY_075Z08

#ifdef GDEW_075T7
#include "gdew075T7.h"

#elif defined GDEY_075Z08
#include "gdew075Z08.h"

#else
#error "ePaper type not defined!"
#endif

#include "EpdSpi.h"
// #include "gdem029E97.h"

#include <Fonts/Roboto_Condensed_SemiBold40pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold60pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold75pt7b.h>

/* WiFi credentials configured via menuconfig */
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD

#define Y_OFFSET 40
#define LINE_SPACING 50

extern EpdSpi io;
#ifdef GDEW_075T7
extern Gdew075T7 display;

#elif defined GDEY_075Z08
extern Gdew075Z08 display;

#else
#error "ePaper type not defined!"
#endif

void display_start_screen(void);

void display_message_data(const uint8_t *data, int data_len);

uint16_t print_centered_line(const char *text, uint16_t startY, uint16_t availWidth);

const GFXfont *select_font_for_text(const char *text, uint16_t availWidth, uint16_t availHeight);

#endif