
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
#include "esp.h"

#include "freertos/event_groups.h"

#define ML_WIFI_MODE_STA   Val_int(0)
#define ML_WIFI_MODE_AP    Val_int(1)
#define ML_WIFI_MODE_APSTA Val_int(2)

#define ML_WIFI_IF_STA     Val_int(0)
#define ML_WIFI_IF_AP      Val_int(1)

#define ML_WIFI_AUTH_OPEN              Val_int(0)
#define ML_WIFI_AUTH_WPA_PSK           Val_int(1)
#define ML_WIFI_AUTH_WPA2_PSK          Val_int(2)
#define ML_WIFI_AUTH_WPA_WPA2_PSK      Val_int(3)
#define ML_WIFI_AUTH_WPA2_ENTERPRISE   Val_int(4)

#define ML_WIFI_ERROR_OUT_OF_MEMORY    Val_int(0)



inline value result_ok (value val) {
    CAMLparam0 ();
    CAMLlocal1(res);
    res = caml_alloc_small(2, 0);
    Field(res, 0) = Val_int(0);
    Field(res, 1) = val;
    return res;
}

inline value result_fail (value error) {
    CAMLparam0 ();
    CAMLlocal1(res);
    res = caml_alloc_small(2, 0);
    Field(res, 0) = Val_int(1);
    Field(res, 1) = error;
    return res;
}

CAMLprim 
value ml_wifi_initialize(value unit) {
    CAMLparam0 ();

    /* Initialize event loop with wifi_event_handler */
    if (esp_event_loop_init(wifi_event_handler, NULL) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }
    
    /* Allocate buffers for wifi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init_internal(&cfg) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    /* Set settings storage mode in ram. */
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_deinitialize(value unit) {
    CAMLparam0 ();

    if (esp_wifi_deinit() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_set_mode(value mode) {
    CAMLparam1 (mode);

    wifi_mode_t esp_mode;
    switch (mode) {
        case ML_WIFI_MODE_STA:
            esp_mode = WIFI_MODE_STA;
            break;
        case ML_WIFI_MODE_AP:
            esp_mode = WIFI_MODE_AP;
            break;
        case ML_WIFI_MODE_APSTA:
            esp_mode = WIFI_MODE_APSTA;
            break;
    }

    if (esp_wifi_set_mode(esp_mode) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_get_mode(value unit) {
    CAMLparam0 ();

    CAMLlocal1 (mode);

    wifi_mode_t esp_mode;

    if (esp_wifi_get_mode(&esp_mode) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    switch (esp_mode) {
        case WIFI_MODE_STA:
            mode = ML_WIFI_MODE_STA;
            break;
        case WIFI_MODE_AP:
            mode = ML_WIFI_MODE_AP;
            break;
        case WIFI_MODE_APSTA:
            mode = ML_WIFI_MODE_APSTA;
            break;
    }

    CAMLreturn (result_ok(mode));
}

CAMLprim 
value ml_wifi_start(value unit) {
    CAMLparam0 ();

    if (esp_wifi_start() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_stop(value unit) {
    CAMLparam0 ();

    if (esp_wifi_stop() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_ap_set_config(value config) {
    CAMLparam1 (config);

    wifi_sta_config_t ap_config;

    /* copy ssid */
    int len = caml_string_length(Field(config, 0));
    if (len > 32) {
        CAMLreturn (result_fail(0));
    }
    memcpy(&ap_config.ssid, Bytes_val(Field(config, 0)), len);

    /* copy password */
    int len = caml_string_length(Field(config, 1));
    if (len > 64) {
        CAMLreturn (result_fail(0));
    }
    memcpy(&ap_config.password, Bytes_val(Field(config, 1)), len);

    ap_config.ssid_len = len;

    int channel = Int_val(Field(config, 2));
    int auth_mode = Int_val(Field(config, 3));
    int ssid_hidden = Int_val(Field(config, 4));
    int max_connection = Int_val(Field(config, 5));
    int beacon_interval = Int_val(Field(config, 6));

    if (channel > 256 || channel < 0) {
        CAMLreturn (result_fail(0));
    }
    ap_config.channel = (uint8_t) channel;

    switch (auth_mode) {
        case ML_WIFI_AUTH_OPEN:
            ap_config.auth_mode = WIFI_AUTH_OPEN;
            break;
        case ML_WIFI_AUTH_WPA_PSK:
            ap_config.auth_mode = WIFI_AUTH_WPA_PSK;
            break;
        case ML_WIFI_AUTH_WPA2_PSK:
            ap_config.auth_mode = WIFI_AUTH_WPA2_PSK;
            break;
        case ML_WIFI_AUTH_WPA_WPA2_PSK:
            ap_config.auth_mode = WIFI_AUTH_WPA_WPA2_PSK;
            break;
        case ML_WIFI_AUTH_WPA2_ENTERPRISE:
            ap_config.auth_mode = WIFI_AUTH_WPA2_ENTERPRISE;
            break;
        default:
            CAMLreturn (result_fail(0));
    }

    ap_config.ssid_hidden = ssid_hidden;
    ap_config.max_connection = (uint8_t) max_connection;
    ap_config.beacon_interval = (uint8_t) beacon_interval;

    wifi_config_t config = {
        .ap = ap_config
    };

    if (esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_ap_get_config(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_sta_set_config(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_sta_get_config(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_connect(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_disconnect(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_start(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_stop(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_count(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_get_list(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_read(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_write(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_get_mac(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_sta_get_ap_info(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_ap_deauth_sta(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_ap_get_sta_list(value unit) {
    CAMLparam0 ();

    CAMLreturn (result_ok(Val_unit));
}



