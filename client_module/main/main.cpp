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
#include "text_decode_utils.h"
#include "wifi.h"
#include "display.h"
#include "battery.h"
#include "mbedtls/base64.h"

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// ESP-NOW image data
#define EINK_W 800
#define EINK_H 480
#define LOGO_BUF_SIZE ((EINK_W * EINK_H) / 8)
// buffer & write-offset for incoming logo chunks
static uint8_t logo_buf[LOGO_BUF_SIZE];
static size_t logo_offset = 0;

static const char *TAG = "ESP-NOW RX";

extern "C"
{
    void app_main();
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

void esp_now_recv_callback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    // ── JSON-based control (clear/text) ────────────────────────────────
    if (data_len > 0 && data[0] == '{')
    {
        // existing JSON handling (clear screen or draw text)...
        char *json_str = (char *)malloc(data_len + 1);
        if (!json_str)
        {
            ESP_LOGE(TAG, "OOM allocating JSON buffer");
            return;
        }
        memcpy(json_str, data, data_len);
        json_str[data_len] = '\0';

        cJSON *root = cJSON_Parse(json_str);
        free(json_str);
        if (!root)
        {
            ESP_LOGE(TAG, "Malformed JSON");
            return;
        }

        // Clear display?
        if (cJSON_GetObjectItemCaseSensitive(root, "clear"))
        {
            gpio_set_level(GPIO_NUM_2, 1);
            display.fillScreen(EPD_WHITE);
            display.update();
            gpio_set_level(GPIO_NUM_2, 0);
            cJSON_Delete(root);
            return;
        }

        // 1b) Text update
        cJSON *first_item = cJSON_GetObjectItemCaseSensitive(root, "first_name");
        cJSON *last_item = cJSON_GetObjectItemCaseSensitive(root, "last_name");
        cJSON *add_item = cJSON_GetObjectItemCaseSensitive(root, "additional_info");

        const char *first = cJSON_IsString(first_item) ? first_item->valuestring : "";
        const char *last = cJSON_IsString(last_item) ? last_item->valuestring : "";
        const char *add = cJSON_IsString(add_item) ? add_item->valuestring : "";

        char *first_clean = remove_diacritics_utf8(first);
        char *last_clean = remove_diacritics_utf8(last);
        char *add_clean = remove_diacritics_utf8(add);

        gpio_set_level(GPIO_NUM_2, 1);
        display.fillScreen(EPD_WHITE);
        display.setTextColor(EPD_BLACK);

        uint16_t w = display.width(), h = display.height();
        uint16_t y = Y_OFFSET, ls = LINE_SPACING, nh = 150;

        if (first_clean[0])
        {
            display.setFont(selectFontForText(first_clean, w, nh));
            y += printCenteredLine(first_clean, y, w) + ls;
        }
        if (last_clean[0])
        {
            display.setFont(selectFontForText(last_clean, w, nh));
            y += printCenteredLine(last_clean, y, w) + ls;
        }
        if (add_clean[0])
        {
            display.setFont(&Roboto_Condensed_SemiBold40pt7b);
            int16_t tbx, tby;
            uint16_t tbw, tbh;
            display.getTextBounds(add_clean, 0, 0, &tbx, &tby, &tbw, &tbh);
            uint16_t yy = h - tbh - 20;
            int16_t xx = (w - tbw) / 2 - tbx;
            display.setCursor(xx, yy + tbh);
            display.println(add_clean);
        }

        float bat_voltage = measure_batt_voltage();
        char bat_str[6];
        sprintf(bat_str, "%0.2fV", bat_voltage / 1000.0f);
        display.setFont(NULL);
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.println(bat_str);

        display.update();
        gpio_set_level(GPIO_NUM_2, 0);

        free(first_clean);
        free(last_clean);
        free(add_clean);
        cJSON_Delete(root);
        return;
    }

    // ── Binary logo chunks ──────────────────────────────────────────────
    // sanity check
    if (logo_offset + data_len > LOGO_BUF_SIZE)
    {
        ESP_LOGE(TAG, "Logo buffer overflow: %u + %d > %u",
                 logo_offset, data_len, (unsigned)LOGO_BUF_SIZE);
        logo_offset = 0;
        return;
    }

    // copy incoming chunk
    memcpy(logo_buf + logo_offset, data, data_len);
    logo_offset += data_len;

    // if we've received the full image, render it
    if (logo_offset >= LOGO_BUF_SIZE)
    {
        ESP_LOGI(TAG, "Full logo received (%u bytes), rendering…",
                 (unsigned)logo_offset);

        gpio_set_level(GPIO_NUM_2, 1);
        display.fillScreen(EPD_WHITE);
        // Draw raw 1-bit bitmap at (0,0)
        display.drawBitmap(0, 0, logo_buf, EINK_W, EINK_H, EPD_BLACK);
        display.update();
        gpio_set_level(GPIO_NUM_2, 0);

        // reset for next transfer
        logo_offset = 0;
    }
}

void app_main(void)
{
    // init wi-fi and esp-now
    wifi_sta_init();
    esp_now_register_recv_cb(esp_now_recv_callback);
    adc_init();

    // power for EInk display
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);
    display_start_screen();
    while (1)
    {
        // Turn off power for the EInk display
        gpio_set_level(GPIO_NUM_2, 0);
        // wait 6 hours
        vTaskDelay(pdMS_TO_TICKS(60000 * 60 * 6));
        // Turn on power for the EInk display
        gpio_set_level(GPIO_NUM_2, 1);
        display.update();
        ESP_LOGI(TAG, "Display refreshed.\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    adc_deinit();
}
