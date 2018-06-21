
#include <string.h>

#define CAML_NAME_SPACE
#include <caml/alloc.h>
#include <caml/memory.h>
#include <caml/signals.h>
#include <caml/fail.h>
#include <caml/callback.h>
#include <caml/bigarray.h>

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_wifi_internal.h"

#include "freertos/event_groups.h"

#include "wifi.h"


/* Event group to notify Mirage task when data is received*/
EventGroupHandle_t  esp_event_group;
int                 esp_event_offset;

static wifi_status wifi_current_status = {
    .wifi_inited     = 0,
    .ap_started      = 0,
    .sta_started     = 0,
    .sta_connected   = 0
};

void wifi_set_event_group(EventGroupHandle_t event_group, int offset) {
    esp_event_group = event_group;
    esp_event_offset = offset;

    xEventGroupSetBits(esp_event_group, ESP_STA_STOPPED_BIT << esp_event_offset);
    xEventGroupSetBits(esp_event_group, ESP_STA_DISCONNECTED_BIT << esp_event_offset);
    xEventGroupSetBits(esp_event_group, ESP_AP_STOPPED_BIT << esp_event_offset);
}

static uint32_t n_sta_frames = 0;
static uint32_t n_ap_frames = 0;

esp_err_t sta_packet_handler(void *buffer, uint16_t len, void *eb);
esp_err_t ap_packet_handler(void *buffer, uint16_t len, void *eb);

void wifi_wait_for_event(int event_bitset) {
    EventGroupHandle_t local_event_group;
    if (esp_event_group == NULL) {
        if (wifi_current_status.ap_started && (ESP_AP_STARTED_BIT & event_bitset)){
            return;
        }
        if (wifi_current_status.sta_connected && (ESP_STA_CONNECTED_BIT & event_bitset)) {
            return;
        }
        if (wifi_current_status.sta_started && (ESP_STA_STARTED_BIT & event_bitset)) {
            return;
        }
        if (n_ap_frames > 0 && (ESP_AP_FRAME_RECEIVED_BIT & event_bitset)) {
            return;
        }
        if (n_sta_frames > 0 && (ESP_STA_FRAME_RECEIVED_BIT & event_bitset)) {
            return;
        }

        local_event_group = xEventGroupCreate();
    }

    wifi_set_event_group(local_event_group, 0);
    xEventGroupWaitBits(esp_event_group, event_bitset, pdFALSE, pdFALSE, 300*configTICK_RATE_HZ);

    if (local_event_group != 0) {
        vEventGroupDelete(local_event_group);
    }
}

wifi_status wifi_get_status() {
    return wifi_current_status;
}

esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    printf("Wifi event: %d\n", event->event_id);
    switch(event->event_id) {
        /* Station events */
        case SYSTEM_EVENT_STA_START:
            ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, sta_packet_handler));
            wifi_current_status.sta_started = true;
            if (esp_event_group != NULL) {
                xEventGroupSetBits(esp_event_group, ESP_STA_STARTED_BIT << esp_event_offset);
                xEventGroupClearBits(esp_event_group, ESP_STA_STOPPED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_STA_STOP:
            wifi_current_status.sta_started = false;
            if (esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_STA_STARTED_BIT << esp_event_offset);
                xEventGroupSetBits(esp_event_group, ESP_STA_STOPPED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            wifi_current_status.sta_connected = true;
            if (esp_event_group != NULL) {
                xEventGroupSetBits(esp_event_group, ESP_STA_CONNECTED_BIT << esp_event_offset);
                xEventGroupClearBits(esp_event_group, ESP_STA_DISCONNECTED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            wifi_current_status.sta_connected = false;
            if (esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_STA_CONNECTED_BIT << esp_event_offset);
                xEventGroupSetBits(esp_event_group, ESP_STA_DISCONNECTED_BIT << esp_event_offset);
            }
            break;
        /* AP events */
        case SYSTEM_EVENT_AP_START:
            wifi_current_status.ap_started = true;
            ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_AP, ap_packet_handler));
            if (esp_event_group != NULL) {
                xEventGroupSetBits(esp_event_group, ESP_AP_STARTED_BIT << esp_event_offset);
                xEventGroupClearBits(esp_event_group, ESP_AP_STOPPED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_AP_STOP:
            wifi_current_status.ap_started = false;
            if (esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_AP_STARTED_BIT << esp_event_offset);
                xEventGroupSetBits(esp_event_group, ESP_AP_STOPPED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_AP_STACONNECTED:
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            break;
    }
    return ESP_OK;
}

/*
 Wifi frame descriptors storage. 
 */
typedef struct frame_list {
    uint16_t length;
    void* buffer;
    void* l2_frame; /* the whole frame, to free with `esp_wifi_internal_free_rx_buffer` after transmmission to the stack. */
} wifi_frame_t;

static QueueHandle_t ap_frames;
static QueueHandle_t sta_frames;

static const MAX_NUMBER_OF_FRAMES = 20;

esp_err_t wifi_initialize() {
    esp_err_t res;

    ap_frames = xQueueCreate(MAX_NUMBER_OF_FRAMES, sizeof(wifi_frame_t));
    sta_frames = xQueueCreate(MAX_NUMBER_OF_FRAMES, sizeof(wifi_frame_t));

    ESP_ERROR_CHECK(nvs_flash_init());

    /* Initialize event loop with wifi_event_handler */
    if (res = esp_event_loop_init(wifi_event_handler, NULL) != ESP_OK) {
        return res;
    }
    
    /* Allocate buffers for wifi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (res = esp_wifi_init_internal(&cfg) != ESP_OK) {
        return res;
    }

    /* Set settings storage mode in ram. */
    if (res = esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return res;
    }

    wifi_current_status.wifi_inited = true;

    return ESP_OK;
}

esp_err_t wifi_deinitialize() {
    esp_err_t res;
    if (res = esp_wifi_deinit() != ESP_OK) {
        return res;
    }
    wifi_current_status.wifi_inited = false;
    return ESP_OK;
}



esp_err_t sta_packet_handler(void *buffer, uint16_t len, void *eb) {
    wifi_frame_t tmp_buffer;

    /* drop one frame */
    if (uxQueueMessagesWaitingFromISR(ap_frames) == MAX_NUMBER_OF_FRAMES) {
        printf("[wifi] Too many STA frames pending, dropping the oldest one.\r");
        xQueueReceiveFromISR(sta_frames, &tmp_buffer, NULL);
        esp_wifi_internal_free_rx_buffer(tmp_buffer.l2_frame);
    }

    tmp_buffer.buffer = buffer;
    tmp_buffer.length = len;
    tmp_buffer.l2_frame = eb;
    xQueueSendFromISR(sta_frames, &tmp_buffer, NULL);
    xEventGroupSetBits(esp_event_group, ESP_STA_FRAME_RECEIVED_BIT << esp_event_offset);
    return ESP_OK;
}


esp_err_t ap_packet_handler(void *buffer, uint16_t len, void *eb) {
    wifi_frame_t tmp_buffer;

    /* drop one frame */
    if (uxQueueMessagesWaitingFromISR(ap_frames) == MAX_NUMBER_OF_FRAMES) {
        printf("[wifi] Too many AP frames pending, dropping the oldest one.\r");
        xQueueReceiveFromISR(ap_frames, &tmp_buffer, NULL);
        esp_wifi_internal_free_rx_buffer(tmp_buffer.l2_frame);
    }

    tmp_buffer.buffer = buffer;
    tmp_buffer.length = len;
    tmp_buffer.l2_frame = eb;
    xQueueSendFromISR(ap_frames, &tmp_buffer, NULL);
    xEventGroupSetBits(esp_event_group, ESP_AP_FRAME_RECEIVED_BIT << esp_event_offset);
    return ESP_OK;
}

int wifi_read(wifi_interface_t interface, uint8_t* buf, size_t* size) {
    int result;
    wifi_frame_t tmp_buffer;

    QueueHandle_t frames;

    int bit_to_set;

    if (interface == WIFI_IF_AP) {
        frames = ap_frames;
        bit_to_set = ESP_AP_FRAME_RECEIVED_BIT << esp_event_offset;
    } else if (interface == WIFI_IF_STA) {
        frames = sta_frames;
        bit_to_set = ESP_STA_FRAME_RECEIVED_BIT << esp_event_offset;
    } else {
        assert(false);
    }

    if(xQueueReceive(frames, &tmp_buffer, 0)) {
        if (tmp_buffer.length > *size) {
            result = WIFI_ERR_INVAL;
            *size = 0;
        } else {
            result = WIFI_ERR_OK;
            *size = tmp_buffer.length;
            memcpy(buf, tmp_buffer.buffer, tmp_buffer.length);
        }
        esp_wifi_internal_free_rx_buffer(tmp_buffer.l2_frame);

        /* Update event group status. */
        if (uxQueueMessagesWaiting(frames) == 0 && esp_event_group != NULL) {
            /* Between those two lines an ISR can happen. So after that we'll make sure that if a frame has been received the event has been registered.*/
            xEventGroupClearBits(esp_event_group, bit_to_set);
            if (uxQueueMessagesWaiting(frames) >= 1) {
                xEventGroupSetBits(esp_event_group, bit_to_set);
            }
        }
    } else {
        result = WIFI_ERR_AGAIN;
        *size = 0;
    }

    return result;
}

/*
 lwIP error codes
 */
#define ERR_OK 0
#define ERR_ARG -16

int wifi_write(wifi_interface_t interface, uint8_t* buf, size_t* size) {
    int result;
    switch(esp_wifi_internal_tx(interface, buf, *size)){
        case ERR_OK:
            result = WIFI_ERR_OK;
            break;
        case ERR_ARG:
            result = WIFI_ERR_INVAL;
            break;
        default:
            result = WIFI_ERR_UNSPEC;
            break;
    }
    return result;
}
