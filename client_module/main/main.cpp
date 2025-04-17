#include <stdio.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "string.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "driver/gpio.h"
#include <driver/adc.h>
#include "esp_adc_cal.h"
#include "cJSON.h"
#include "qrcode.h"
#include "text_decode_utils.h"

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "gdem029E97.h"
#include "gdew075T7.h"

EpdSpi io;
// Gdem029E97 display(io); // 2.9 inch
Gdew075T7 display(io); // 7.5 inch grayscale

#include <Fonts/Roboto_Condensed_SemiBold40pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold60pt7b.h>
#include <Fonts/Roboto_Condensed_SemiBold75pt7b.h>

#define Y_OFFSET 40
#define LINE_SPACING 50
#define ADC_CHANNEL ADC1_CHANNEL_6 // GPIO34 corresponds to ADC1_CHANNEL_6
#define ADC_ATTEN ADC_ATTEN_DB_11
#define ADC_WIDTH ADC_WIDTH_BIT_12
#define DEFAULT_VREF 1100 // Default Vref in mV; calibrate if possible
#define NO_OF_SAMPLES 64  // Number of samples for averaging

uint8_t esp_mac[6];
static const char *TAG = "ESP-NOW RX";

extern "C"
{
    void app_main();
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

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

    //  Display first name
    if (strlen(first_name) > 0)
    {
        const GFXfont *fontForFirst = selectFontForText(first_name_clean, dispWidth, availHeightForName);
        display.setFont(fontForFirst);
        currentY += printCenteredLine(first_name_clean, currentY, dispWidth);
        currentY += lineSpacing; // extra gap after the first name
    }

    //  Display last name
    if (strlen(last_name) > 0)
    {
        const GFXfont *fontForLast = selectFontForText(last_name_clean, dispWidth, availHeightForName);
        display.setFont(fontForLast);
        currentY += printCenteredLine(last_name_clean, currentY, dispWidth);
        currentY += lineSpacing;
    }

    //  Display additional info
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
    // Clear the display.
    display.fillScreen(EPD_WHITE);
    int dispW = display.width(), dispH = display.height();
    int leftW = dispW / 2;

    //  Left Region: Wi-Fi Credentials and QR Code
    display.setTextSize(3);
    const char *leftLines[] = {
        "1) Connect to Wi-Fi:",
        "Meet Ink Controller",
        "password: hesloheslo",
        "or",
        "scan QR to connect"};
    const int nLeft = sizeof(leftLines) / sizeof(leftLines[0]);
    int leftY = 5;
    int16_t tx, ty;
    uint16_t tw, th;
    // Print each left line, centered in the left half.
    for (int i = 0; i < nLeft; i++)
    {
        display.getTextBounds(leftLines[i], 0, 0, &tx, &ty, &tw, &th);
        int xPos = (leftW - tw) / 2;
        if (i < 3)
        {
            display.setCursor(xPos, leftY + th);
            leftY += th + 10;
        }
        else
        {
            display.setCursor(xPos, leftY + 2 * th);
            leftY += 2 * th + 10;
        }
        display.println(leftLines[i]);
    }

    //  QR Code: Scaled to about 200x200 pixels
    int qrTop = leftY + 40;
    int qrSize = esp_qrcode_get_size(qrcode);
    // Set module size such that overall QR code is approximately 200 pixels wide.
    int moduleSize = 200 / qrSize;
    if (moduleSize < 1)
    {
        moduleSize = 1;
    }
    int qrDrawSize = qrSize * moduleSize;
    int qrX = (leftW - qrDrawSize) / 2;

    // Draw each QR module.
    for (int y = 0; y < qrSize; y++)
    {
        for (int x = 0; x < qrSize; x++)
        {
            int pixelX = qrX + x * moduleSize;
            int pixelY = qrTop + y * moduleSize;
            bool module = esp_qrcode_get_module(qrcode, x, y);
            display.fillRect(pixelX, pixelY, moduleSize, moduleSize, module ? EPD_BLACK : EPD_WHITE);
        }
    }

    //  Right Region: Instructional Text
    int rightX = dispW / 2, rightW = dispW - rightX - 10;
    const char *rightLines[] = {
        "2) Access the",
        "webpage on URL:",
        "meetink.local",
        "or",
        "http://192.168.4.1"};
    const int nRight = sizeof(rightLines) / sizeof(rightLines[0]);
    display.getTextBounds("Ag", 0, 0, &tx, &ty, &tw, &th);
    int lineHeight = th, spacing = 10;
    int totalHeight = nRight * lineHeight + (nRight - 1) * spacing;
    int rightY = 5;

    auto centerAndPrint = [&](const char *txt, int yPos)
    {
        display.getTextBounds(txt, 0, 0, &tx, &ty, &tw, &th);
        int xPos = rightX + (rightW - tw) / 2;
        display.setCursor(xPos, yPos + th);
        display.println(txt);
    };

    for (int i = 0; i < nRight; i++)
    {
        if (i < 2)
        {
            centerAndPrint(rightLines[i], rightY + i * (lineHeight + spacing));
        }
        else
        {
            centerAndPrint(rightLines[i], rightY + i * (lineHeight + spacing + 20));
        }
    }
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
    cfg.display_func = qr_eink_display;        // Use our custom display callback
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED; // Set medium error correction level

    // Define the Wi‑Fi credentials using the standard QR code format.
    const char *qrText = "WIFI:T:WPA;S:Meet Ink Controller;P:hesloheslo;;";

    // Generate and display the QR code.
    esp_err_t ret = esp_qrcode_generate(&cfg, qrText);
    if (ret == ESP_OK)
    {
        printf("QR code generated and displayed on the EInk.\n");
    }
    else
    {
        printf("Failed to generate QR code. Error: %d\n", ret);
    }
    // Turn off power for the EInk display
    gpio_set_level(GPIO_NUM_2, 0);
}
