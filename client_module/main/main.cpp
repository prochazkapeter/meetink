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

#include <Fonts/Roboto_Condensed_SemiBold40pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold60pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold75pt7b.h>

#define Y_OFFSET 40
#define LINE_SPACING 50

uint8_t esp_mac[6];
static const char *TAG = "ESP-NOW RX";

extern "C"
{
    void app_main();
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

static const GFXfont *selectFontForText(const char *text, uint16_t availWidth, uint16_t availHeight)
{
    const GFXfont *fonts[3] = {
        &Roboto_Condensed_SemiBold75pt7b,
        &Roboto_Condensed_SemiBold60pt7b,
        &Roboto_Condensed_SemiBold40pt7b};

    int16_t tbx, tby;
    uint16_t tbw, tbh;
    for (int i = 0; i < 3; i++)
    {
        display.setFont(fonts[i]);
        display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
        ESP_LOGI(TAG, "tbw %d tbh: %d\n", tbw, tbh);
        if (tbw <= availWidth && tbh <= availHeight)
        {
            ESP_LOGI(TAG, "Chosen font: %d\n", i);
            return fonts[i];
        }
    }
    ESP_LOGI(TAG, "Chosen smallest font\n");
    return &Roboto_Condensed_SemiBold40pt7b;
}

static uint16_t printCenteredLine(const char *text, uint16_t startY, uint16_t availWidth)
{
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(text, 0, startY, &tbx, &tby, &tbw, &tbh);
    uint16_t x = (availWidth - tbw) / 2 - tbx;
    display.setCursor(x, startY + tbh); // Adjust y to account for text baseline
    display.println(text);
    return tbh;
}

void esp_now_recv_callback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    char *json_str = (char *)malloc(data_len + 1);
    if (json_str == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed");
        return;
    }

    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0'; // terminating zero for string

    ESP_LOGI(TAG, "Received string: %s", json_str);

    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        free(json_str);
        return;
    }

    // extract data from JSON
    cJSON *first_name_item = cJSON_GetObjectItemCaseSensitive(json, "first_name");
    cJSON *last_name_item = cJSON_GetObjectItemCaseSensitive(json, "last_name");
    cJSON *additional_info_item = cJSON_GetObjectItemCaseSensitive(json, "additional_info");

    const char *first_name = (cJSON_IsString(first_name_item) && first_name_item->valuestring) ? first_name_item->valuestring : "";
    const char *last_name = (cJSON_IsString(last_name_item) && last_name_item->valuestring) ? last_name_item->valuestring : "";
    const char *additional_info = (cJSON_IsString(additional_info_item) && additional_info_item->valuestring) ? additional_info_item->valuestring : "";

    ESP_LOGI(TAG, "Parsed JSON - First Name: %s, Last Name: %s, Additional Info: %s",
             first_name, last_name, additional_info);

    // turn on display power
    gpio_set_level(GPIO_NUM_2, 1);
    // Update the e-ink display
    display.fillScreen(EPD_WHITE);
    // Set font and text color as needed
    display.setTextColor(EPD_BLACK);

    uint16_t dispWidth = display.width();
    uint16_t dispHeight = display.height();
    uint16_t currentY = Y_OFFSET;
    uint16_t lineSpacing = LINE_SPACING;
    uint16_t availHeightForName = 150;

    // --- Display first name ---
    if (strlen(first_name) > 0)
    {
        const GFXfont *fontForFirst = selectFontForText(first_name, dispWidth, availHeightForName);
        display.setFont(fontForFirst);
        currentY += printCenteredLine(first_name, currentY, dispWidth);
        currentY += lineSpacing; // extra gap after the first name
    }

    // --- Display last name ---
    if (strlen(last_name) > 0)
    {
        const GFXfont *fontForLast = selectFontForText(last_name, dispWidth, availHeightForName);
        display.setFont(fontForLast);
        currentY += printCenteredLine(last_name, currentY, dispWidth);
        currentY += lineSpacing;
    }

    // --- Display additional info ---
    if (strlen(additional_info) > 0)
    {
        display.setFont(&Roboto_Condensed_SemiBold40pt7b);
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds(additional_info, 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t infoY = dispHeight - tbh - 20;
        int16_t infoX = (dispWidth - tbw) / 2 - tbx;
        display.setCursor(infoX, infoY + tbh);
        display.println(additional_info);
    }
    // update display
    display.update();

    // Cleanup JSON and free allocated memory
    cJSON_Delete(json);
    free(json_str);
    // turn off display power
    gpio_set_level(GPIO_NUM_2, 0);
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
    display.setTextColor(EPD_BLACK);
    display.fillScreen(EPD_WHITE);
    display.update();
    gpio_set_level(GPIO_NUM_2, 0);
}
