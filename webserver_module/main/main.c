#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_system.h>
#include "esp_now.h"

#include <driver/adc.h>
#include "esp_adc_cal.h"

#include "webserver.h"
#include "text_decode_utils.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ESP_MAC_1 {0x34, 0x5f, 0x45, 0x2d, 0xb1, 0x68}
#define STA_CHANNEL 1

/* WiFi credentials configured via menuconfig */
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#define ADC_CHANNEL ADC1_CHANNEL_6 // GPIO34 corresponds to ADC1_CHANNEL_6
#define ADC_ATTEN ADC_ATTEN_DB_11
#define ADC_WIDTH ADC_WIDTH_BIT_12
#define DEFAULT_VREF 1100 // Default Vref in mV; calibrate if possible
#define NO_OF_SAMPLES 64  // Number of samples for averaging

static const char *TAG = "webserver";

/* Compile-time mode selection:
 * Set WIFI_MODE to 1 for Access Point mode,
 * or 0 for Station mode (connect to existing WiFi).
 */
#ifndef WIFI_MODE
#define WIFI_MODE 1
#endif

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

float getBatteryVoltage(void)
{
    static esp_adc_cal_characteristics_t adc_chars;
    static bool is_calibrated = false;

    // Configure ADC1 only once
    if (!is_calibrated)
    {
        adc1_config_width(ADC_WIDTH);
        adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);
        // Characterize ADC at attenuation level
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH, DEFAULT_VREF, &adc_chars);
        is_calibrated = true;
    }

    uint32_t adc_reading = 0;
    // Multisampling
    for (int i = 0; i < NO_OF_SAMPLES; i++)
    {
        adc_reading += adc1_get_raw(ADC_CHANNEL);
    }
    adc_reading /= NO_OF_SAMPLES;

    // Convert ADC reading to voltage in millivolts
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);

    // Return voltage in volts
    return voltage_mv / 1000.0f;
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

/* Station Mode Functions */

static void wifi_sta_start_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Station mode started, attempting to connect...");
    esp_wifi_connect();
}

static void wifi_sta_disconnect_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Station disconnected, trying to reconnect...");
    esp_wifi_connect();
}

static void wifi_sta_got_ip_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    if (*server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
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
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Create a local static server handle to be used in the event handler */
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_START,
                                                        &wifi_sta_start_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_DISCONNECTED,
                                                        &wifi_sta_disconnect_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_sta_got_ip_handler,
                                                        &server,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "StarLink",
            .password = "ferdaferda",
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // uint8_t my_esp_mac[6] = {0xd8, 0x13, 0x2a, 0x00, 0xdc, 0xf4};
    // esp_read_mac(my_esp_mac, ESP_MAC_WIFI_STA);
    // ESP_LOGI(TAG, "peer mac " MACSTR "", my_esp_mac[0], my_esp_mac[1], my_esp_mac[2], my_esp_mac[3], my_esp_mac[4], my_esp_mac[5]);

    ESP_LOGI(TAG, "wifi_init_sta finished. Connecting to SSID:%s password:%s",
             wifi_config.sta.ssid, wifi_config.sta.password);
}

void esp_now_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status)
{

    ESP_LOGI(TAG, "tx cb");
}

void init_esp_now(void)
{
    esp_now_init();
    esp_now_register_send_cb(esp_now_send_callback);
    esp_now_peer_info_t peer_info = {0};
    peer_info.channel = STA_CHANNEL;
    peer_info.encrypt = false;
    uint8_t esp_mac[6] = ESP_MAC_1;

    memcpy(peer_info.peer_addr, esp_mac, 6);
    esp_now_add_peer(&peer_info);
}

void app_main(void)
{
    static httpd_handle_t server = NULL;

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if WIFI_MODE
    ESP_LOGI(TAG, "Starting in Access Point mode");
    wifi_init_softap();
    init_esp_now();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_AP_STAIPASSIGNED,
                                               &connect_handler,
                                               &server));
#else
    ESP_LOGI(TAG, "Starting in Station mode");
    wifi_init_sta();
    init_esp_now();
#endif
}
