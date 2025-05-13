#include "wifi.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "webserver.h"

static const char *TAG = "wifi";

/* Function to initialize mDNS */
static void init_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err)
    {
        ESP_LOGE(TAG, "mDNS Init failed: %d", err);
        return;
    }
    mdns_hostname_set("meetink");
    mdns_instance_name_set("ESP32 Web Server");
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err)
    {
        ESP_LOGE(TAG, "mDNS service add failed: %d", err);
    }
    ESP_LOGI(TAG, "mDNS initialized");
}

/* AP Mode Functions */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

// When a station connects and gets an IP (as seen on the AP interface),
// start the webserver and initialize mDNS.
static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    static bool mdns_initialized = false;
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
        if (!mdns_initialized)
        {
            init_mdns();
            mdns_initialized = true;
        }
    }
}

void add_peer(uint8_t esp_mac[6])
{
    esp_now_peer_info_t peer_info = {0};
    peer_info.channel = STA_CHANNEL;
    peer_info.encrypt = false;

    memcpy(peer_info.peer_addr, esp_mac, 6);
    ESP_LOGI(TAG, "esp_now_add_peer: %x:%x:%x:%x:%x:%x", esp_mac[0], esp_mac[1], esp_mac[2], esp_mac[3], esp_mac[4], esp_mac[5]);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
}

void delete_peer(uint8_t esp_mac[6])
{
    ESP_LOGI(TAG, "esp_now_del_peer: %hhx:%hhx:%hhx:%hhx:%hhx:%hhx", esp_mac[0], esp_mac[1], esp_mac[2], esp_mac[3], esp_mac[4], esp_mac[5]);
    ESP_ERROR_CHECK(esp_now_del_peer(esp_mac));
}

void init_esp_now(void)
{
    ESP_ERROR_CHECK(esp_now_init());

    // Open NVS namespace where we keep mac_0…mac_N
    nvs_handle_t nvs;
    if (nvs_open("mac_store", NVS_READONLY, &nvs) != ESP_OK)
    {
        // on error, return
        return;
    }

    char key[16];
    char mac[20];
    size_t len;
    for (int i = 0; i < MAX_MAC_ENTRIES; i++)
    {
        // Read the i-th MAC entry
        snprintf(key, sizeof(key), "mac_%d", i);
        len = sizeof(mac);
        if (nvs_get_str(nvs, key, mac, &len) != ESP_OK)
        {
            continue; // slot empty
        }
        uint8_t parsed_mac[6];
        if (parse_mac(mac, parsed_mac))
        {
            add_peer(parsed_mac);
        }
    }

    nvs_close(nvs);
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = STA_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);

    init_esp_now();
    static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_AP_STAIPASSIGNED,
                                               &connect_handler,
                                               &server));
}
