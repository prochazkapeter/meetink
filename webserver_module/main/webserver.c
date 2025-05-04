#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_now.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_content.h" // Contains the index_html constant

#include "mbedtls/base64.h"

static const char *TAG = "EInkREST";

#define MAX_MAC_ENTRIES 20
#define EINK_W 800
#define EINK_H 480
#define ESP_NOW_MAX_DATA_LEN 250
// one bit per pixel
#define LOGO_BUF_SIZE ((EINK_W * EINK_H) / 8)
#define MAC_STR_LEN 17               // "AA:BB:CC:DD:EE:FF"
#define HEADER_LEN (MAC_STR_LEN + 1) // + '\n'
static uint8_t logo_buf[LOGO_BUF_SIZE];
// static const uint8_t target_mac[6] = {0x34, 0x5F, 0x45, 0x2D, 0xB1, 0x68};

char *generate_mac_blocks_html()
{
    // Pre-allocate ~6 KiB for all blocks
    char *output = calloc(1, 6144);
    if (!output)
    {
        return NULL;
    }
    output[0] = '\0';

    // Open NVS namespace where we keep mac_0…mac_N
    nvs_handle_t nvs;
    if (nvs_open("mac_store", NVS_READONLY, &nvs) != ESP_OK)
    {
        // on error, return empty page
        return output;
    }

    char key[16];
    char mac[20];
    size_t len;
    char block[2048];

    for (int i = 0; i < MAX_MAC_ENTRIES; i++)
    {
        // Read the i-th MAC entry
        snprintf(key, sizeof(key), "mac_%d", i);
        len = sizeof(mac);
        if (nvs_get_str(nvs, key, mac, &len) != ESP_OK)
        {
            continue; // slot empty
        }

        // Generate the combined badge + logo HTML block
        snprintf(block, sizeof(block),
                 "<div class=\"badge-block\" data-mac=\"%s\">"
                 "<h3>%s</h3>"
                 "<form onsubmit=\"sendText(event,'%s')\">"
                 "<input type=\"text\" name=\"first_name\" placeholder=\"First Name\">"
                 "<input type=\"text\" name=\"last_name\" placeholder=\"Last Name\">"
                 "<input type=\"text\" name=\"additional_info\" placeholder=\"Additional Info\">"
                 "<div style=\"display:flex; gap:8px; margin-top:8px;\">"
                 "<button type=\"submit\">Send</button>"
                 "<button type=\"button\" class=\"clear\" onclick=\"clearBadge('%s')\">Clear</button>"
                 "<button type=\"button\" class=\"delete\" onclick=\"deleteMac('%s')\">Delete</button>"
                 "</div>"
                 "</form>"
                 "<div class=\"logo-block\">"
                 "<h3>Image Upload</h3>"
                 "<input type=\"file\" id=\"logoInput_%s\" accept=\"image/*\">"
                 "<canvas id=\"logoPreview_%s\" width=\"800\" height=\"480\"></canvas>"
                 "<button id=\"sendLogoBtn_%s\" class=\"send-logo-btn\" disabled>Send Image</button>"
                 "</div>"
                 "</div>",
                 mac, // data-mac
                 mac, // <h3>%s</h3>
                 mac, // sendText(event,'%s')
                 mac, // clearBadge('%s')
                 mac, // deleteMac('%s')
                 mac, // id="logoInput_%s"
                 mac, // id="logoPreview_%s"
                 mac  // id="sendLogoBtn_%s"
        );
        strcat(output, block);
    }

    nvs_close(nvs);
    return output;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char *html_template = (const char *)webcontent_html;
    const char *placeholder = "<!-- {{MAC_LIST}} -->";
    const char *injection_point = strstr(html_template, placeholder);

    if (!injection_point)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "MAC list placeholder not found");
    }

    char *mac_html = generate_mac_blocks_html();
    if (!mac_html)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to generate MAC block");
    }

    size_t pre_len = injection_point - html_template;
    size_t mac_html_len = strlen(mac_html);
    size_t placeholder_len = strlen(placeholder);
    size_t post_len = webcontent_html_len - pre_len - placeholder_len;
    size_t total_len = pre_len + mac_html_len + post_len;

    char *final_html = malloc(total_len);
    if (!final_html)
    {
        free(mac_html);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory");
    }

    memcpy(final_html, html_template, pre_len);
    memcpy(final_html + pre_len, mac_html, mac_html_len);
    memcpy(final_html + pre_len + mac_html_len,
           injection_point + placeholder_len,
           post_len);

    free(mac_html);

    httpd_resp_set_type(req, "text/html");
    esp_err_t res = httpd_resp_send(req, final_html, total_len);
    free(final_html);
    return res;
}

static esp_err_t sendtext_post_handler(httpd_req_t *req)
{
    char *buf = malloc(req->content_len + 1);
    if (!buf)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");

    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive post data");
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    // Get MAC address
    cJSON *mac_item = cJSON_GetObjectItemCaseSensitive(json, "mac");
    if (!cJSON_IsString(mac_item) || !mac_item->valuestring)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing MAC in body");
    }

    const char *mac_str = mac_item->valuestring;
    ESP_LOGI(TAG, "Target MAC string from JSON: %s", mac_str);

    uint8_t target_mac[6];
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &target_mac[0], &target_mac[1], &target_mac[2],
               &target_mac[3], &target_mac[4], &target_mac[5]) != 6)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
    }

    // Parse other fields
    const char *first_name = "";
    const char *last_name = "";
    const char *additional_info = "";

    cJSON *first_name_item = cJSON_GetObjectItemCaseSensitive(json, "first_name");
    cJSON *last_name_item = cJSON_GetObjectItemCaseSensitive(json, "last_name");
    cJSON *additional_info_item = cJSON_GetObjectItemCaseSensitive(json, "additional_info");

    if (cJSON_IsString(first_name_item) && first_name_item->valuestring)
        first_name = first_name_item->valuestring;
    if (cJSON_IsString(last_name_item) && last_name_item->valuestring)
        last_name = last_name_item->valuestring;
    if (cJSON_IsString(additional_info_item) && additional_info_item->valuestring)
        additional_info = additional_info_item->valuestring;

    if (strlen(first_name) == 0 && strlen(last_name) == 0 && strlen(additional_info) == 0)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "At least one field must be provided");
    }

    ESP_LOGI(TAG, "Sending to MAC %02X:%02X:%02X:%02X:%02X:%02X -> First: %s, Last: %s, Info: %s",
             target_mac[0], target_mac[1], target_mac[2],
             target_mac[3], target_mac[4], target_mac[5],
             first_name, last_name, additional_info);

    // Format and send
    cJSON *send_json = cJSON_CreateObject();
    cJSON_AddStringToObject(send_json, "first_name", first_name);
    cJSON_AddStringToObject(send_json, "last_name", last_name);
    cJSON_AddStringToObject(send_json, "additional_info", additional_info);
    char *send_str = cJSON_PrintUnformatted(send_json);
    cJSON_Delete(send_json);
    cJSON_Delete(json);

    esp_err_t err = esp_now_send(target_mac, (uint8_t *)send_str, strlen(send_str));
    free(send_str);

    // Response
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_json, "status", esp_err_to_name(err));
    char *resp_str = cJSON_PrintUnformatted(resp_json);
    cJSON_Delete(resp_json);

    httpd_resp_set_type(req, "application/json");
    esp_err_t res = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);

    return res;
}

static esp_err_t addmac_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    cJSON *mac_item = cJSON_GetObjectItemCaseSensitive(json, "mac");
    if (!cJSON_IsString(mac_item) || !mac_item->valuestring)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing MAC string");
    }

    const char *mac_str = mac_item->valuestring;
    ESP_LOGI("AddMAC", "Received MAC: %s", mac_str);

    // Validate MAC format: AA:BB:CC:DD:EE:FF
    uint8_t mac_bin[6];
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac_bin[0], &mac_bin[1], &mac_bin[2],
               &mac_bin[3], &mac_bin[4], &mac_bin[5]) != 6)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
    }

    nvs_handle_t nvs;
    if (nvs_open("mac_store", NVS_READWRITE, &nvs) != ESP_OK)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
    }

    // Check for duplicates first
    char key[16], existing[20];
    size_t len;
    bool already_registered = false;

    for (int i = 0; i < MAX_MAC_ENTRIES; i++)
    {
        snprintf(key, sizeof(key), "mac_%d", i);
        len = sizeof(existing);
        if (nvs_get_str(nvs, key, existing, &len) == ESP_OK)
        {
            if (strcasecmp(existing, mac_str) == 0)
            {
                already_registered = true;
                break;
            }
        }
    }

    if (already_registered)
    {
        nvs_close(nvs);
        cJSON_Delete(json);
        ESP_LOGI("AddMAC", "This MAC is already registered");
        return httpd_resp_send(req, "Already registered", HTTPD_RESP_USE_STRLEN);
    }

    // Store in the first empty slot
    for (int i = 0; i < MAX_MAC_ENTRIES; i++)
    {
        snprintf(key, sizeof(key), "mac_%d", i);
        len = sizeof(existing);
        if (nvs_get_str(nvs, key, existing, &len) == ESP_ERR_NVS_NOT_FOUND)
        {
            esp_err_t err = nvs_set_str(nvs, key, mac_str);
            if (err == ESP_OK)
            {
                nvs_commit(nvs);
                nvs_close(nvs);
                cJSON_Delete(json);
                ESP_LOGI("AddMAC", "MAC is saved");
                return httpd_resp_send(req, "MAC saved", HTTPD_RESP_USE_STRLEN);
            }
            else
            {
                nvs_close(nvs);
                cJSON_Delete(json);
                ESP_LOGE("AddMAC", "Failed to save MAC");
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save MAC");
            }
        }
    }

    // Should never reach here
    nvs_close(nvs);
    cJSON_Delete(json);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unexpected failure");
}

static esp_err_t deletemac_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    cJSON *mac_item = cJSON_GetObjectItemCaseSensitive(json, "mac");
    if (!cJSON_IsString(mac_item) || !mac_item->valuestring)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing MAC");
    }

    const char *target_mac = mac_item->valuestring;
    ESP_LOGI("DeleteMAC", "Attempting to delete: %s", target_mac);

    nvs_handle_t nvs;
    if (nvs_open("mac_store", NVS_READWRITE, &nvs) != ESP_OK)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open NVS");
    }

    char key[16], value[20];
    size_t len;
    bool found = false;

    for (int i = 0; i < MAX_MAC_ENTRIES; i++)
    {
        snprintf(key, sizeof(key), "mac_%d", i);
        len = sizeof(value);
        if (nvs_get_str(nvs, key, value, &len) == ESP_OK)
        {
            if (strcasecmp(value, target_mac) == 0)
            {
                ESP_LOGI("DeleteMAC", "Found at %s. Deleting...", key);
                esp_err_t err = nvs_erase_key(nvs, key);
                if (err == ESP_OK)
                {
                    nvs_commit(nvs);
                    found = true;
                }
                break;
            }
        }
    }

    nvs_close(nvs);
    cJSON_Delete(json);

    if (found)
    {
        return httpd_resp_send(req, "MAC deleted", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        ESP_LOGE("DeleteMAC", "Delete mac failed");
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "MAC not found");
    }
}

static esp_err_t clearbadge_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    cJSON *mac_item = cJSON_GetObjectItemCaseSensitive(json, "mac");
    if (!cJSON_IsString(mac_item) || !mac_item->valuestring)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing MAC");
    }

    const char *mac_str = mac_item->valuestring;
    ESP_LOGI("ClearBadge", "Attempting to clear: %s", mac_str);

    uint8_t target_mac[6];
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &target_mac[0], &target_mac[1], &target_mac[2],
               &target_mac[3], &target_mac[4], &target_mac[5]) != 6)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
    }

    // Format and send
    cJSON *send_json = cJSON_CreateObject();
    cJSON_AddStringToObject(send_json, "clear", "1");
    char *send_str = cJSON_PrintUnformatted(send_json);
    cJSON_Delete(send_json);
    cJSON_Delete(json);

    esp_err_t err = esp_now_send(target_mac, (uint8_t *)send_str, strlen(send_str));
    free(send_str);

    // Response
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_json, "status", esp_err_to_name(err));
    char *resp_str = cJSON_PrintUnformatted(resp_json);
    cJSON_Delete(resp_json);

    httpd_resp_set_type(req, "application/json");
    esp_err_t res = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);

    return res;
}

static bool parse_mac(const char *s, uint8_t mac[6])
{
    int vals[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x",
               &vals[0], &vals[1], &vals[2],
               &vals[3], &vals[4], &vals[5]) == 6)
    {
        for (int i = 0; i < 6; ++i)
            mac[i] = (uint8_t)vals[i];
        return true;
    }
    return false;
}

static void send_logo_task(void *arg)
{
    struct
    {
        uint8_t addr[6];
        size_t len;
    } *p = arg;

    size_t total = p->len;
    size_t offset = 0;
    int pid = 0;

    while (offset < total)
    {
        size_t chunk = (total - offset > 250) ? 250 : (total - offset);
        esp_err_t err = esp_now_send(p->addr, logo_buf + offset, chunk);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "ESP-NOW pkt %d failed: %d", pid, err);
        }
        offset += chunk;
        pid++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "All %d chunks sent to %02X:%02X:%02X:%02X:%02X:%02X",
             pid, p->addr[0], p->addr[1], p->addr[2],
             p->addr[3], p->addr[4], p->addr[5]);

    free(p);
    vTaskDelete(NULL);
}

/**
 * HTTP POST /sendlogo
 *   • Reads up to LOGO_BUF_SIZE bytes into logo_buf
 *   • Replies “200 OK” immediately
 *   • Spawns send_logo_task() so ESP-NOW happens off the HTTP thread
 */
static esp_err_t sendlogo_post_handler(httpd_req_t *req)
{
    size_t remaining = req->content_len;
    if (remaining < HEADER_LEN || remaining > HEADER_LEN + LOGO_BUF_SIZE)
    {
        ESP_LOGE(TAG, "Bad length: %u", (unsigned)remaining);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad payload size");
        return ESP_FAIL;
    }

    // 1) Read the MAC header + '\n'
    char mac_hdr[HEADER_LEN + 1];
    size_t to_read = HEADER_LEN;
    char *hdr_ptr = mac_hdr;
    while (to_read)
    {
        int r = httpd_req_recv(req, hdr_ptr, to_read);
        if (r <= 0)
        {
            if (r == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            ESP_LOGE(TAG, "Header recv err: %d", r);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Header error");
            return ESP_FAIL;
        }
        hdr_ptr += r;
        to_read -= r;
    }
    mac_hdr[HEADER_LEN] = '\0'; // NUL-terminate
    // strip trailing newline
    if (mac_hdr[MAC_STR_LEN] != '\n')
    {
        ESP_LOGE(TAG, "Missing newline after MAC");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed header");
        return ESP_FAIL;
    }
    mac_hdr[MAC_STR_LEN] = '\0';

    uint8_t peer_mac[6];
    if (!parse_mac(mac_hdr, peer_mac))
    {
        ESP_LOGE(TAG, "Invalid MAC: %s", mac_hdr);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC");
        return ESP_FAIL;
    }

    // 2) Read the remaining bytes into logo_buf
    size_t logo_len = remaining - HEADER_LEN;
    uint8_t *bufptr = logo_buf;
    while (logo_len)
    {
        int r = httpd_req_recv(req, (char *)bufptr, logo_len);
        if (r <= 0)
        {
            if (r == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            ESP_LOGE(TAG, "Logo recv err: %d", r);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Data error");
            return ESP_FAIL;
        }
        bufptr += r;
        logo_len -= r;
    }
    ESP_LOGI(TAG, "Got %u logo bytes for %s",
             (unsigned)(remaining - HEADER_LEN), mac_hdr);

    // 3) Immediate HTTP response
    httpd_resp_sendstr(req, "Logo uploaded");

    // 4) Spawn ESP-NOW send task
    struct
    {
        uint8_t addr[6];
        size_t len;
    } *task_arg = malloc(sizeof(*task_arg));

    if (task_arg)
    {
        memcpy(task_arg->addr, peer_mac, 6);
        task_arg->len = remaining - HEADER_LEN;
        xTaskCreate(send_logo_task, "send_logo", 2048, task_arg, 5, NULL);
    }
    else
    {
        ESP_LOGE(TAG, "OOM allocating task arg");
    }

    return ESP_OK;
}

// URI handler definitions
static const httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t sendtext_uri = {
    .uri = "/sendtext",
    .method = HTTP_POST,
    .handler = sendtext_post_handler,
    .user_ctx = NULL};

static const httpd_uri_t addmac_uri = {
    .uri = "/addmac",
    .method = HTTP_POST,
    .handler = addmac_post_handler,
    .user_ctx = NULL};

static const httpd_uri_t deletemac_uri = {
    .uri = "/deletemac",
    .method = HTTP_POST,
    .handler = deletemac_post_handler,
    .user_ctx = NULL};

static const httpd_uri_t clearbadge_uri = {
    .uri = "/clearbadge",
    .method = HTTP_POST,
    .handler = clearbadge_post_handler,
    .user_ctx = NULL};

httpd_uri_t sendlogo_uri = {
    .uri = "/sendlogo",
    .method = HTTP_POST,
    .handler = sendlogo_post_handler,
    .user_ctx = NULL};

// Starts the HTTP server and registers URI handlers
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &sendtext_uri);
        httpd_register_uri_handler(server, &addmac_uri);
        httpd_register_uri_handler(server, &deletemac_uri);
        httpd_register_uri_handler(server, &clearbadge_uri);
        httpd_register_uri_handler(server, &sendlogo_uri);
        ESP_LOGI(TAG, "HTTP server started successfully");
        return server;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}
