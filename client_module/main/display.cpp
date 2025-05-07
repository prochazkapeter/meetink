
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "qrcode.h"
#include "battery.h"
#include "text_decode_utils.h"

EpdSpi io;
// Gdem029E97 display(io); // 2.9 inch
Gdew075T7 display(io); // 7.5 inch

// ESP-NOW image data
#define EINK_W 800
#define EINK_H 480
#define LOGO_BUF_SIZE ((EINK_W * EINK_H) / 8)
// buffer & write-offset for incoming logo chunks
static uint8_t logo_buf[LOGO_BUF_SIZE];
static size_t logo_offset = 0;

static const char *TAG = "DISPLAY";

void display_message_data(const uint8_t *data, int data_len)
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
            logo_offset = 0;
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

        if (add_clean && strcmp(add_clean, "reset666") == 0)
        {
            esp_restart(); // soft-reset the chip
        }

        gpio_set_level(GPIO_NUM_2, 1);
        display.fillScreen(EPD_WHITE);
        display.setTextColor(EPD_BLACK);

        uint16_t w = display.width(), h = display.height();
        uint16_t y = Y_OFFSET, ls = LINE_SPACING, nh = 150;

        if (first_clean[0])
        {
            display.setFont(select_font_for_text(first_clean, w, nh));
            y += print_centered_line(first_clean, y, w) + ls;
        }
        if (last_clean[0])
        {
            display.setFont(select_font_for_text(last_clean, w, nh));
            y += print_centered_line(last_clean, y, w) + ls;
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

uint16_t print_centered_line(const char *text, uint16_t startY, uint16_t availWidth)
{
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(text, 0, startY, &tbx, &tby, &tbw, &tbh);
    uint16_t x = (availWidth - tbw) / 2 - tbx;
    display.setCursor(x, startY + tbh); // Adjust y to account for text baseline
    display.println(text);
    return tbh;
}

const GFXfont *select_font_for_text(const char *text, uint16_t availWidth, uint16_t availHeight)
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
static void qr_eink_display(esp_qrcode_handle_t qrcode)
{
    // Clear the display.
    display.fillScreen(EPD_WHITE);
    int dispW = display.width();
    int leftW = dispW / 2;

    //  Left Region: Wi-Fi Credentials and QR Code
    display.setTextSize(3);
    const char *leftLines[] = {
        "1) Connect to Wi-Fi:",
        EXAMPLE_ESP_WIFI_SSID,
        "password: " EXAMPLE_ESP_WIFI_PASS,
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
        "http://192.168.4.1",
        "3) Register display:",
    };
    const int nRight = sizeof(rightLines) / sizeof(rightLines[0]);
    display.getTextBounds("Ag", 0, 0, &tx, &ty, &tw, &th);
    int lineHeight = th, spacing = 10;
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
        else if (i < 5)
        {
            centerAndPrint(rightLines[i], rightY + i * (lineHeight + spacing + 20));
        }
        else
        {
            centerAndPrint(rightLines[i], rightY + i * (lineHeight + spacing + 20) + 50);
        }
    }
    static char mac_str[18];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    centerAndPrint(mac_str, rightY + (nRight) * (lineHeight + spacing + 20) + 50);
    // print battery voltage
    float bat_voltage = measure_batt_voltage();
    static char bat_str[6];
    sprintf(bat_str, "%0.2fV", bat_voltage / 1000.0f);
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println(bat_str);
    display.update();
}

void display_start_screen(void)
{
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
    const char *qrText = "WIFI:T:WPA;S:" EXAMPLE_ESP_WIFI_SSID ";P:" EXAMPLE_ESP_WIFI_PASS ";;";
    // Generate and display the QR code.
    esp_err_t ret = esp_qrcode_generate(&cfg, qrText);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "QR code generated and displayed on the EInk.\n");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to generate QR code. Error: %d\n", ret);
    }
}
