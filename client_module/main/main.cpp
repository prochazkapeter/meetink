#include <stdio.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "string.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "driver/gpio.h"
#include "cJSON.h"

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

#include "gdem029E97.h"
#include "gdew075T7.h"

EpdSpi io;
// Gdem029E97 display(io); // 2.9 inch
Gdew075T7 display(io); // 7.5 inch grayscale

// #include <Fonts/ubuntu/Ubuntu_M12pt8b.h>
// #include <Fonts/DMSerifText_Regular20pt7b.h>
#include <Fonts/DMSerifText_Regular24pt7b.h>
// #include <Fonts/Roboto_Condensed_Bold84pt7b.h>
// #include <Fonts/FreeSansBold80pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold75pt7b.h>

#define FONT_USED Roboto_Condensed_SemiBold75pt7b
#define FONT_SMALL DMSerifText_Regular24pt7b

extern "C"
{
    void app_main();
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint8_t esp_mac[6];
static const char *TAG = "ESP-NOW RX";
void esp_now_recv_callback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    char *json_str = (char *)malloc(data_len + 1);
    if (json_str == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed");
        return;
    }

    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0'; // správné zakončení stringu

    ESP_LOGI(TAG, "Received string: %s", json_str);

    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        free(json_str);
        return;
    }

    // Pointery z JSONu
    cJSON *first_name_item = cJSON_GetObjectItemCaseSensitive(json, "first_name");
    cJSON *last_name_item = cJSON_GetObjectItemCaseSensitive(json, "last_name");
    cJSON *additional_info_item = cJSON_GetObjectItemCaseSensitive(json, "additional_info");

    const char *first_name = (cJSON_IsString(first_name_item) && first_name_item->valuestring) ? first_name_item->valuestring : "";
    const char *last_name = (cJSON_IsString(last_name_item) && last_name_item->valuestring) ? last_name_item->valuestring : "";
    const char *additional_info = (cJSON_IsString(additional_info_item) && additional_info_item->valuestring) ? additional_info_item->valuestring : "";

    ESP_LOGI(TAG, "Parsed JSON - First Name: %s, Last Name: %s, Additional Info: %s",
             first_name, last_name, additional_info);

    // Update the e-ink display
    display.fillScreen(EPD_WHITE);
    // Set font and text color as needed
    display.setFont(&FONT_USED);
    display.setTextColor(EPD_BLACK);

    // Layout idea:
    // - Display first and last name at the top (centered) with a large font.
    // - Display additional info in a smaller font below.
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    uint16_t x, y = 0;

    // Display first name (if provided)
    if (strlen(first_name) > 0)
    {
        display.getTextBounds(first_name, 0, 0, &tbx, &tby, &tbw, &tbh);
        x = (display.width() - tbw) / 2 - tbx;
        y = 1.3 * tbh;
        display.setCursor(x, y);
        display.println(first_name);
    }

    // Display last name (if provided) just below first name
    if (strlen(last_name) > 0)
    {
        display.getTextBounds(last_name, 0, 0, &tbx, &tby, &tbw, &tbh);
        x = (display.width() - tbw) / 2 - tbx;
        y += 1.5 * tbh;
        display.setCursor(x, y);
        display.println(last_name);
    }

    // Display additional info (if provided) in the lower part of the screen
    display.setFont(&FONT_SMALL);
    if (strlen(additional_info) > 0)
    {
        display.getTextBounds(additional_info, 0, 0, &tbx, &tby, &tbw, &tbh);
        x = (display.width() - tbw) / 2 - tbx;
        y = display.height() - tbh;
        display.setCursor(x, y);
        display.println(additional_info);
    }

    display.update();
    // free data
    cJSON_Delete(json);
    free(json_str);
}
void wifi_sta_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    esp_read_mac(esp_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "peer mac " MACSTR "", esp_mac[0], esp_mac[1], esp_mac[2], esp_mac[3], esp_mac[4], esp_mac[5]);
}
void app_main(void)
{
    wifi_sta_init();
    esp_now_init();
    esp_now_register_recv_cb(esp_now_recv_callback);

    // power for EInk display
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);

    ESP_LOGI(TAG, "CalEPD version %s", CALEPD_VERSION);
    display.init(false);
    display.setRotation(2);
    display.setFont(&FONT_USED);
    display.setTextColor(EPD_BLACK);
    display.fillScreen(EPD_WHITE);
    display.update();
    // gpio_set_level(GPIO_NUM_2, 0);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
