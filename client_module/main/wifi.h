#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "esp_err.h"

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