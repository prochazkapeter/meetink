#ifndef WIFI_H
#define WIFI_H_H

#include <stdint.h>

/* WiFi credentials configured via menuconfig */
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#define ESP_MAC_1 {0x34, 0x5f, 0x45, 0x2d, 0xb1, 0x68}
#define STA_CHANNEL 1

void wifi_init_softap(void);

void add_peer(uint8_t esp_mac[6]);

void delete_peer(uint8_t esp_mac[6]);

#endif // WEBSERVER_H