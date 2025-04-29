#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_now.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_content.h" // Contains the index_html constant

static const char *TAG = "EInkREST";

#define MAX_MAC_ENTRIES 20
// static const uint8_t target_mac[6] = {0x34, 0x5F, 0x45, 0x2D, 0xB1, 0x68};

char *generate_mac_blocks_html()
{
    char *output = calloc(1, 6144);
    if (!output)
        return NULL;
    output[0] = '\0';

    nvs_handle_t nvs;
    if (nvs_open("mac_store", NVS_READONLY, &nvs) != ESP_OK)
    {
        return output;
    }

    char key[16], mac[20];
    size_t len;

    for (int i = 0; i < MAX_MAC_ENTRIES; i++)
    {
        snprintf(key, sizeof(key), "mac_%d", i);
        len = sizeof(mac);
        esp_err_t err = nvs_get_str(nvs, key, mac, &len);
        if (err == ESP_OK)
        {
            char block[1024];
            // TODO: add clear text button
            snprintf(block, sizeof(block),
                     "<div class='mac-block'>"
                     "<h3>Client id: %s</h3>"
                     "<form onsubmit='sendText(event, \"%s\")'>"
                     "<input type='text' name='first_name' placeholder='First Name'><br>"
                     "<input type='text' name='last_name' placeholder='Last Name'><br>"
                     "<input type='text' name='additional_info' placeholder='Additional Info'><br>"
                     "<button type='submit'>Send</button>"
                     "<button type='button' onclick='clearBadge(\"%s\")' style='margin-left:10px;background:#c96f10;'>Clear</button>"
                     "<button type='button' onclick='deleteMac(\"%s\")' style='margin-left:10px;background:#e74c3c;'>Delete</button>"
                     "</form></div>",
                     mac, mac, mac, mac);
            strcat(output, block);
        }
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
