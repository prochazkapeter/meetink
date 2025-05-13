#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_http_server.h"

#define MAX_MAC_ENTRIES 20
#define EINK_W 800
#define EINK_H 480
#define ESP_NOW_MAX_DATA_LEN 250
// one bit per pixel
#define LOGO_BUF_SIZE ((EINK_W * EINK_H) / 8)
#define MAC_STR_LEN 17               // "AA:BB:CC:DD:EE:FF"
#define HEADER_LEN (MAC_STR_LEN + 1) // + '\n'

// Starts the HTTP server and returns the server handle.
httpd_handle_t start_webserver(void);

bool parse_mac(const char *s, uint8_t mac[6]);

#endif // WEBSERVER_H
