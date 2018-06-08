#ifndef ESP32_WIFI_H
#define ESP32_WIFI_H

#include "esp_system.h"
#include "esp_event.h"

#define WIFI_ERR_OK     0
#define WIFI_ERR_AGAIN  1
#define WIFI_ERR_INVAL  2
#define WIFI_ERR_UNSPEC 3

const int ESP_STA_STARTED_BIT       = BIT0;
const int ESP_AP_STARTED_BIT        = BIT1;
const int ESP_STA_CONNECTED_BIT     = BIT2;
const int ESP_STA_FRAME_RECEIVED_BIT= BIT3;
const int ESP_AP_FRAME_RECEIVED_BIT = BIT4;

typedef struct wifi_status {
    unsigned int wifi_inited    : 1;
    unsigned int ap_started     : 1;
    unsigned int sta_started    : 1;
    unsigned int sta_connected  : 1;
} wifi_status;

esp_err_t wifi_initialize();
esp_err_t wifi_deinitialize();

wifi_status wifi_get_status();
void wifi_set_event_group(EventGroupHandle_t event_group, int offset);

int wifi_read(wifi_interface_t interface, uint8_t* buf, size_t* size);
int wifi_write(wifi_interface_t interface, uint8_t* buf, size_t* size);

void wifi_wait_for_event(int event_bitset);

#endif