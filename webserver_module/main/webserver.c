#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_now.h"
#include "cJSON.h"
#include "web_content.h" // Contains the index_html constant

static const char *TAG = "EInkREST";

// Define the target display(s) MAC addresses (expand as needed)
#define TARGET_DISPLAYS_COUNT 1
static const uint8_t target_mac[6] = {0x34, 0x5F, 0x45, 0x2D, 0xB1, 0x68};

// Handler for GET "/" to serve the HTML page
static esp_err_t index_get_handler(httpd_req_t *req)
{
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

// Handler for POST "/sendtext" to process JSON payload allowing missing fields
static esp_err_t sendtext_post_handler(httpd_req_t *req)
{
    // Allocate buffer for incoming JSON data
    char *buf = malloc(req->content_len + 1);
    if (!buf)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
    }
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive post data");
    }
    buf[ret] = '\0';

    // Parse the received JSON using cJSON
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }

    // Allow missing fields by defaulting to an empty string.
    const char *first_name = "";
    const char *last_name = "";
    const char *additional_info = "";

    cJSON *first_name_item = cJSON_GetObjectItemCaseSensitive(json, "first_name");
    cJSON *last_name_item = cJSON_GetObjectItemCaseSensitive(json, "last_name");
    cJSON *additional_info_item = cJSON_GetObjectItemCaseSensitive(json, "additional_info");

    if (cJSON_IsString(first_name_item) && (first_name_item->valuestring != NULL))
    {
        first_name = first_name_item->valuestring;
    }
    if (cJSON_IsString(last_name_item) && (last_name_item->valuestring != NULL))
    {
        last_name = last_name_item->valuestring;
    }
    if (cJSON_IsString(additional_info_item) && (additional_info_item->valuestring != NULL))
    {
        additional_info = additional_info_item->valuestring;
    }

    // Ensure at least one field is provided
    if (strlen(first_name) == 0 && strlen(last_name) == 0 && strlen(additional_info) == 0)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "At least one field must be provided");
    }

    ESP_LOGI(TAG, "Received JSON - First Name: %s, Last Name: %s, Additional Info: %s",
             first_name, last_name, additional_info);

    // Create a new JSON object to format the message sent via ESP-NOW.
    cJSON *send_json = cJSON_CreateObject();
    cJSON_AddStringToObject(send_json, "first_name", first_name);
    cJSON_AddStringToObject(send_json, "last_name", last_name);
    cJSON_AddStringToObject(send_json, "additional_info", additional_info);
    char *send_str = cJSON_PrintUnformatted(send_json);
    cJSON_Delete(send_json);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Formatted JSON to send(length without /0 %d): %s", strlen(send_str), send_str);

    size_t json_len = strlen(send_str);
    // Send the formatted JSON string via ESP-NOW to each target display
    for (int i = 0; i < json_len; i++)
    {
        printf("%02X ", send_str[i]);
    }
    printf("\n");
    esp_err_t err = esp_now_send(target_mac, (uint8_t *)send_str, json_len);
    ESP_LOGI(TAG, "ESP-NOW send to display status: %s", esp_err_to_name(err));

    free(send_str);

    // Respond with a JSON confirmation
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_json, "status", "ok");
    char *resp_str = cJSON_PrintUnformatted(resp_json);
    cJSON_Delete(resp_json);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret_err = httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);

    return ret_err;
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
