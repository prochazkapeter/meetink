#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "esp_err.h"

#define CONFIG_ESPNOW_ENABLE_POWER_SAVE 0
// range 0 - 65535 in milliseconds
#define CONFIG_ESPNOW_WAKE_WINDOW 50
// range 1 - 65535 in milliseconds
#define CONFIG_ESPNOW_WAKE_INTERVAL 100

    /**
     * @brief Initialize Wi-Fi in station mode and set up ESP-NOW receive.
     *
     * Must be called before esp_now_init().
     */
    void wifi_sta_init(void);

#ifdef __cplusplus
}
#endif

#endif