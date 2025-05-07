#include "esp_now.h"
#include "wifi.h"
#include "display.h"
#include "battery.h"

// FreeRTOS queue
#define ESPNOW_MAX_PAYLOAD 250
typedef struct
{
    uint8_t mac[ESP_NOW_ETH_ALEN]; // 6-byte MAC of the sender
    int len;                       // real length of `data`
    uint8_t data[ESPNOW_MAX_PAYLOAD];
} espnow_evt_t;

static QueueHandle_t espnow_queue;

static const char *TAG = "RX-MAIN";

extern "C"
{
    void app_main();
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

/**
 * @brief ISR-level ESP-NOW RX callback that hand-offs packets to a worker task.
 *
 * Copies a received frame (≤ #ESPNOW_MAX_PAYLOAD) into a local espnow_evt_t
 * and posts it to the global @c espnow_queue with xQueueSendFromISR().
 * Oversize frames are discarded.
 * If the queue send wakes a higher-priority task, portYIELD_FROM_ISR() is
 * invoked to yield immediately.
 *
 * @param info Driver-supplied metadata (sender MAC, RSSI, …).
 * @param data Pointer to the payload.
 * @param len  Payload length in bytes.
 *
 * @note Runs in ISR/Wi-Fi context—keep it short, allocation-free and use only
 *       FromISR() APIs.
 */
static void esp_now_recv_callback(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len > ESPNOW_MAX_PAYLOAD)
        return;

    espnow_evt_t evt;
    memcpy(evt.mac, info->src_addr, ESP_NOW_ETH_ALEN);
    evt.len = len;
    memcpy(evt.data, data, len);

    BaseType_t xHigherPrioTaskWoken = pdFALSE;
    xQueueSendFromISR(espnow_queue, &evt, &xHigherPrioTaskWoken);

    // If the queue send woke a higher-prio task, request a context-switch
    if (xHigherPrioTaskWoken)
        portYIELD_FROM_ISR();
}

/**
 * @brief FreeRTOS worker that handles packets pushed by the ISR callback.
 *
 * Blocks on @c espnow_queue (portMAX_DELAY).
 * Each dequeued ::espnow_evt_t is passed to @c display_message_data() for full
 * JSON/logo parsing and display updates—work that is unsafe in ISR context but
 * fine here.
 *
 * @param arg Unused; pass NULL when creating the task.
 */
static void espnow_worker_task(void *arg)
{
    espnow_evt_t evt;

    while (true)
    {
        if (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) == pdTRUE)
        {
            display_message_data(evt.data, evt.len);
        }
    }
}

void app_main(void)
{
    // init wi-fi and esp-now
    wifi_sta_init();
    espnow_queue = xQueueCreate(30, sizeof(espnow_evt_t));
    assert(espnow_queue);
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_callback));

    /* start the worker */
    xTaskCreatePinnedToCore(espnow_worker_task,
                            "espnow_worker",
                            4096, /* stack size – raise if needed */
                            NULL,
                            4, /* priority > Wi-Fi task (usually 3) */
                            NULL,
                            tskNO_AFFINITY);

    // init adc for battery measurement
    adc_init();

    // power for EInk display
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);
    // display default screen
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
