
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "qrcode.h"
#include "battery.h"

EpdSpi io;
// Gdem029E97 display(io); // 2.9 inch
Gdew075T7 display(io); // 7.5 inch grayscale

static const char *TAG = "DISPLAY";

uint16_t printCenteredLine(const char *text, uint16_t startY, uint16_t availWidth)
{
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(text, 0, startY, &tbx, &tby, &tbw, &tbh);
    uint16_t x = (availWidth - tbw) / 2 - tbx;
    display.setCursor(x, startY + tbh); // Adjust y to account for text baseline
    display.println(text);
    return tbh;
}

const GFXfont *selectFontForText(const char *text, uint16_t availWidth, uint16_t availHeight)
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
