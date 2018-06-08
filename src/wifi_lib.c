
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
EventGroupHandle_t esp_event_group;
int esp_event_offset;

static wifi_status wifi_current_status;

void wifi_set_event_group(EventGroupHandle_t event_group, int offset) {
    esp_event_group = event_group;
    esp_event_offset = offset;
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

esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        /* Station events */
        case SYSTEM_EVENT_STA_START:
            ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, sta_packet_handler));
            wifi_current_status.sta_started = true;
            if (esp_event_group != NULL) {
                xEventGroupSetBits(esp_event_group, ESP_STA_STARTED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_STA_STOP:
            wifi_current_status.sta_started = false;
            if (esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_STA_STARTED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            wifi_current_status.sta_connected = true;
            if (esp_event_group != NULL) {
                xEventGroupSetBits(esp_event_group, ESP_STA_CONNECTED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            wifi_current_status.sta_connected = false;
            if (esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_STA_CONNECTED_BIT << esp_event_offset);
            }
            break;
        /* AP events */
        case SYSTEM_EVENT_AP_START:
            wifi_current_status.ap_started = true;
            ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, ap_packet_handler));
            if (esp_event_group != NULL) {
                xEventGroupSetBits(esp_event_group, ESP_AP_STARTED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_AP_STOP:
            wifi_current_status.ap_started = false;
            if (esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_AP_STARTED_BIT << esp_event_offset);
            }
            break;
        case SYSTEM_EVENT_AP_STACONNECTED:
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            break;
    }
    return ESP_OK;
}

esp_err_t wifi_initialize() {
    esp_err_t res;

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

/*
 Wifi frame descriptors storage. 
 */
typedef struct frame_list {
    struct frame_list* next;
    uint16_t length;
    void* buffer;
    void* l2_frame; /* the whole frame, to free with `esp_wifi_internal_free_rx_buffer` after transmmission to the stack. */
} frame_list_t;

static frame_list_t* ap_frames_start = NULL;
static frame_list_t* ap_frames_end = NULL;

static frame_list_t* sta_frames_start = NULL;
static frame_list_t* sta_frames_end = NULL;


void free_oldest_frame(frame_list_t** frames_start, frame_list_t** frames_end) 
{
    frame_list_t* oldest_frame = *frames_start;

    /* Last frame in the list */
    if (*frames_start == *frames_end) {
        *frames_start = NULL;
        *frames_end = NULL;
    } else {
        *frames_start = oldest_frame->next;
    }
    esp_wifi_internal_free_rx_buffer(oldest_frame->l2_frame);
    free(oldest_frame);
}

void packet_handler(void *buffer, uint16_t len, void *eb, frame_list_t** frames_start, frame_list_t** frames_end) {
    frame_list_t* entry = malloc(sizeof(frame_list_t));
    entry->next = NULL;
    entry->length = len,
    entry->buffer = buffer;
    entry->l2_frame = eb;
    if (*frames_end != NULL) {
        (*frames_end)->next = entry;
    } else {
        *frames_start = entry;
    }
    *frames_end = entry;
}

esp_err_t sta_packet_handler(void *buffer, uint16_t len, void *eb) {
    packet_handler(buffer, len, eb, &sta_frames_start, &sta_frames_start);
    n_sta_frames++;
    if (n_sta_frames > 30) {
        printf("[wifi] Too many STA frames pending, dropping the oldest one.\n");
        free_oldest_frame(&sta_frames_start, &sta_frames_end);
        n_sta_frames--;
    }
    if (esp_event_group != NULL) {
        xEventGroupSetBits(esp_event_group, ESP_STA_FRAME_RECEIVED_BIT << esp_event_offset);
    }
    return ESP_OK;
}

esp_err_t ap_packet_handler(void *buffer, uint16_t len, void *eb) {
    packet_handler(buffer, len, eb, &ap_frames_start, &ap_frames_start);
    n_ap_frames++;
    if (n_ap_frames > 30) {
        printf("[wifi] Too many AP frames pending, dropping the oldest one.\n");
        free_oldest_frame(&ap_frames_start, &ap_frames_end);
        n_ap_frames--;
    }
    if (esp_event_group != NULL) {
        xEventGroupSetBits(esp_event_group, ESP_AP_FRAME_RECEIVED_BIT << esp_event_offset);
    }
    return ESP_OK;
}

int wifi_read(wifi_interface_t interface, uint8_t* buf, size_t* size) {
    int result;

    frame_list_t* current_frame;
     if (interface == WIFI_IF_AP) {
        current_frame = &ap_frames_start;
    } else if (interface == WIFI_IF_STA) {
        current_frame = &sta_frames_start;
    } else {
        assert (false);
    }

    if (current_frame != NULL) {
        /* Check if destination buffer can contain the payload. If not, drop the payload. */
        if (current_frame->length > *size) {
            result = WIFI_ERR_INVAL;
            *size = 0;
        } else {
            result = WIFI_ERR_OK;
            *size = current_frame->length;
            memcpy(buf, current_frame->buffer, current_frame->length);
        }

        if (interface == WIFI_IF_AP) {
            free_oldest_frame(&ap_frames_start, &ap_frames_end);
            n_ap_frames--;
            if (n_ap_frames == 0 && esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_AP_FRAME_RECEIVED_BIT << esp_event_offset);
            }
        } else if (interface == WIFI_IF_STA) {
            free_oldest_frame(&sta_frames_start, &sta_frames_end);
            n_sta_frames--;
            if (n_sta_frames == 0 && esp_event_group != NULL) {
                xEventGroupClearBits(esp_event_group, ESP_STA_FRAME_RECEIVED_BIT << esp_event_offset);
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
    switch(esp_wifi_internal_tx(WIFI_IF_STA, buf, size)){
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
}
