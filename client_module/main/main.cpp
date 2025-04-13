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
#include "qrcode.h"
#include "text_decode_utils.h"

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
    display.setFont(NULL);
    display.setTextSize(1);
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

    // Remove diacritics.
    char *first_name_clean = remove_diacritics_utf8(first_name);
    char *last_name_clean = remove_diacritics_utf8(last_name);
    char *additional_info_clean = remove_diacritics_utf8(additional_info);

    ESP_LOGI(TAG, "Parsed JSON - First Name: %s, Last Name: %s, Additional Info: %s",
             first_name_clean, last_name_clean, additional_info_clean);

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
        const GFXfont *fontForFirst = selectFontForText(first_name_clean, dispWidth, availHeightForName);
        display.setFont(fontForFirst);
        currentY += printCenteredLine(first_name_clean, currentY, dispWidth);
        currentY += lineSpacing; // extra gap after the first name
    }

    // --- Display last name ---
    if (strlen(last_name) > 0)
    {
        const GFXfont *fontForLast = selectFontForText(last_name_clean, dispWidth, availHeightForName);
        display.setFont(fontForLast);
        currentY += printCenteredLine(last_name_clean, currentY, dispWidth);
        currentY += lineSpacing;
    }

    // --- Display additional info ---
    if (strlen(additional_info) > 0)
    {
        display.setFont(&Roboto_Condensed_SemiBold40pt7b);
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds(additional_info_clean, 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t infoY = dispHeight - tbh - 20;
        int16_t infoX = (dispWidth - tbw) / 2 - tbx;
        display.setCursor(infoX, infoY + tbh);
        display.println(additional_info_clean);
    }
    // update display
    display.update();

    // Cleanup JSON and free allocated memory
    cJSON_Delete(json);
    free(json_str);
    free(first_name_clean);
    free(last_name_clean);
    free(additional_info_clean);
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

/**
 * @brief Custom display function to render the QR code and accompanying instructions.
 *
 * The screen is split into two regions:
 *   - Left region: Draws the QR code with the instruction "1) Connect to Wi‑Fi AP" above it.
 *   - Right region: Displays in the vertical center:
 *       "2) Access the webserver controller" and below that:
 *       "meetink.local", "or", "http://192.168.4.1" (each on separate lines).
 *
 * @param qrcode Handle to the generated QR code.
 */
void qr_eink_display(esp_qrcode_handle_t qrcode)
{
    // Clear the display to white.
    display.fillScreen(EPD_WHITE);

    int dispWidth = display.width();
    int dispHeight = display.height();

    // ----- Left Region: QR Code and Top Text -----
    // Define left region width (approximately half the display).
    int leftRegionWidth = dispWidth / 2;

    // Set up the top text for the left region.
    const char *leftTopText = "1) Connect to Wi-Fi";
    int16_t txtX, txtY;
    uint16_t txtW, txtH;
    display.setTextSize(3);
    display.getTextBounds(leftTopText, 0, 0, &txtX, &txtY, &txtW, &txtH);
    // Center the text horizontally in the left region; set a small top margin.
    int leftTextX = (leftRegionWidth - txtW) / 2;
    int leftTextY = txtH + 5; // 5-pixel margin at the top.
    display.setCursor(leftTextX, leftTextY);
    display.println(leftTopText);

    // Determine area available for drawing the QR code (left region).
    // Leave additional margin below the text.
    int qrAreaTop = leftTextY + 50;
    int availableHeight = dispHeight - qrAreaTop - 10; // bottom margin of 10 pixels
    int availableWidth = leftRegionWidth - 20;         // side margins (10 pixels each)

    // Get the QR code matrix size.
    int qrSize = esp_qrcode_get_size(qrcode);
    // Set module size to fit within the available area.
    int moduleSize = (availableWidth < availableHeight ? availableWidth : availableHeight) / qrSize;
    if (moduleSize < 1)
        moduleSize = 1;
    int qrDrawSize = qrSize * moduleSize;
    // Center the QR code horizontally in the left region.
    int qrStartX = ((leftRegionWidth - qrDrawSize) / 2);
    // Place the QR code just below the top text.
    int qrStartY = qrAreaTop;

    // Draw the QR code modules.
    for (int y = 0; y < qrSize; y++)
    {
        for (int x = 0; x < qrSize; x++)
        {
            bool module = esp_qrcode_get_module(qrcode, x, y);
            int pixelX = qrStartX + x * moduleSize;
            int pixelY = qrStartY + y * moduleSize;
            if (module)
            {
                display.fillRect(pixelX, pixelY, moduleSize, moduleSize, EPD_BLACK);
            }
            else
            {
                display.fillRect(pixelX, pixelY, moduleSize, moduleSize, EPD_WHITE);
            }
        }
    }

    // ----- Right Region: Instructional Text -----
    // Define the right region boundaries.
    int rightRegionX = dispWidth / 2;
    int rightRegionWidth = dispWidth - rightRegionX - 10; // 10-pixel right margin

    // Text lines to be displayed.
    const char *rightLine1 = "2) Access the";
    const char *rightLine2 = "webpage on URL:";
    const char *rightLine3 = "meetink.local";
    const char *rightLine4 = "or";
    const char *rightLine5 = "http://192.168.4.1";

    // Determine text height using a sample string.
    int16_t tmpX, tmpY;
    uint16_t tmpW, tmpH;
    display.getTextBounds("Ag", 0, 0, &tmpX, &tmpY, &tmpW, &tmpH);
    int lineHeight = tmpH;
    int lineSpacing = 5; // Spacing between lines

    // Calculate total text block height (4 lines with spacing between them).
    int totalTextHeight = lineHeight * 10 + lineSpacing * 3;
    // Vertically center the text block in the display.
    int rightStartY = (dispHeight - totalTextHeight) / 2;

    // Helper lambda to center text within the right region.
    auto centerAndPrint = [&](const char *text, int yPos)
    {
        int16_t rx, ry;
        uint16_t rw, rh;
        display.getTextBounds(text, 0, 0, &rx, &ry, &rw, &rh);
        int xPos = rightRegionX + (rightRegionWidth - rw) / 2;
        display.setCursor(xPos, yPos + rh);
        display.println(text);
    };

    centerAndPrint(rightLine1, rightStartY);
    centerAndPrint(rightLine2, rightStartY + lineHeight + lineSpacing);
    centerAndPrint(rightLine3, rightStartY + 3 * (lineHeight + lineSpacing));
    centerAndPrint(rightLine4, rightStartY + 5 * (lineHeight + lineSpacing));
    centerAndPrint(rightLine5, rightStartY + 7 * (lineHeight + lineSpacing));

    // Refresh the display so that all drawn elements become visible.
    display.update();
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
    // display.fillScreen(EPD_WHITE);

    // Configure the QR code parameters.
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qr_eink_display; // Use our custom display callback
    // cfg.max_qrcode_version = 3;                // Use a small version since the URL is short
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED; // Set medium error correction level

    // Define the Wi‑Fi credentials using the standard QR code format.
    const char *qrText = "WIFI:T:WPA;S:Meet Ink Controller;P:hesloheslo;;";

    // Generate and display the QR code.
    esp_err_t ret = esp_qrcode_generate(&cfg, qrText);
    if (ret == ESP_OK)
    {
        printf("QR code generated and displayed on the eInk.\n");
    }
    else
    {
        printf("Failed to generate QR code. Error: %d\n", ret);
    }

    display.update();
    gpio_set_level(GPIO_NUM_2, 0);
}
