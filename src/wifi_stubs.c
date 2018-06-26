
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
#include "string.h"

#include "wifi.h"

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

#define ML_WIFI_ERROR_UNSPECIFIED      Val_int(0)
#define ML_WIFI_ERROR_INVALID_ARGUMENT Val_int(1)
#define ML_WIFI_ERROR_OUT_OF_MEMORY    Val_int(2)
#define ML_WIFI_ERROR_NOTHING_TO_READ  Val_int(3)
#define ML_WIFI_ERROR_WIFI_NOT_INITED  Val_int(4)

CAMLprim value 
result_ok (value val) {
    CAMLparam1 (val);
    CAMLlocal1(res);
    res = caml_alloc(1, 0);
    Store_field(res, 0, val);
    CAMLreturn (res);
}

CAMLprim value
result_fail (value error) {
    CAMLparam1 (error);
    CAMLlocal1(res);
    res = caml_alloc(1, 1);
    Store_field(res, 0, error);
    CAMLreturn (res);
}

CAMLprim 
value ml_wifi_initialize(value unit) {
    CAMLparam0 ();
    if (wifi_initialize() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }
    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_deinitialize(value unit) {
    CAMLparam0 ();
    if (wifi_deinitialize() != ESP_OK) {
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

    wifi_ap_config_t ap_config;

    /* copy ssid */
    int len = caml_string_length(Field(config, 0));
    if (len > 32) {
        printf("ml_wifi_ap_set_config: SSID too long (%d)\n", len);
        CAMLreturn (result_fail(0));
    }
    memcpy(&ap_config.ssid, Bytes_val(Field(config, 0)), len+1);
    ap_config.ssid_len = len;

    /* copy password */
    len = caml_string_length(Field(config, 1));
    if (len > 64) {
        printf("ml_wifi_ap_set_config: password too long (%d)\n", len);
        CAMLreturn (result_fail(0));
    }
    memcpy(&ap_config.password, Bytes_val(Field(config, 1)), len+1);


    int channel = Int_val(Field(config, 2));
    int auth_mode = Field(config, 3);
    int ssid_hidden = Int_val(Field(config, 4));
    int max_connection = Int_val(Field(config, 5));
    int beacon_interval = Int_val(Field(config, 6));

    if (channel > 256 || channel < 0) {
        printf("ml_wifi_ap_set_config: wrong channel (%d)\n", channel);
        CAMLreturn (result_fail(0));
    }
    ap_config.channel = (uint8_t) channel;

    switch (auth_mode) {
        case ML_WIFI_AUTH_OPEN:
            ap_config.authmode = WIFI_AUTH_OPEN;
            break;
        case ML_WIFI_AUTH_WPA_PSK:
            ap_config.authmode = WIFI_AUTH_WPA_PSK;
            break;
        case ML_WIFI_AUTH_WPA2_PSK:
            ap_config.authmode = WIFI_AUTH_WPA2_PSK;
            break;
        case ML_WIFI_AUTH_WPA_WPA2_PSK:
            ap_config.authmode = WIFI_AUTH_WPA_WPA2_PSK;
            break;
        case ML_WIFI_AUTH_WPA2_ENTERPRISE:
            ap_config.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
            break;
        default:
            printf("ml_wifi_ap_set_config: failed parsing auth_mode (%d)\n", auth_mode);
            CAMLreturn (result_fail(0));
    }

    ap_config.ssid_hidden = ssid_hidden;
    ap_config.max_connection = (uint8_t) max_connection;
    ap_config.beacon_interval = (uint8_t) beacon_interval;

    wifi_config_t esp_config = {
        .ap = ap_config
    };

    if (esp_wifi_set_config(WIFI_IF_AP, &esp_config) != ESP_OK) {
        printf("ml_wifi_ap_set_config: failed esp_wifi_set_config (%d)\n", esp_wifi_set_config(WIFI_IF_AP, &esp_config));
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_ap_get_config(value unit) {
    CAMLparam0 ();
    CAMLlocal1 (ml_config);

    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_AP, &config) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }
    ml_config = caml_alloc_tuple(7);

    /* Copy ssid */
    Store_field(ml_config, 0, 
                caml_alloc_initialized_string(config.ap.ssid_len, config.ap.ssid));
    /* Copy password */
    int len = strlen(config.ap.password);
    Store_field(ml_config, 1, 
                caml_alloc_initialized_string(len, config.ap.password));
    /* Copy channel */
    Store_field(ml_config, 2,
                Val_int(config.ap.channel));
    /* Copy auth_mode */
    value ml_auth_mode;
    switch (config.ap.authmode) {
        case WIFI_AUTH_OPEN:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_OPEN);
            break;
        case WIFI_AUTH_WPA_PSK:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA_PSK);
            break;
        case WIFI_AUTH_WPA2_PSK:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA2_PSK);
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA_WPA2_PSK);
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA2_ENTERPRISE);
            break;
    }
    Store_field(ml_config, 3, ml_auth_mode);
    /* Copy ssid_hidden */
    Store_field(ml_config, 4, Val_bool(config.ap.ssid_hidden));
    /* Copy max_connection */
    Store_field(ml_config, 5, Val_int(config.ap.max_connection));
    /* Copy beacon_interval */
    Store_field(ml_config, 6, Val_int(config.ap.beacon_interval));

    CAMLreturn (result_ok(ml_config));
}

CAMLprim 
value ml_wifi_sta_set_config(value config) {
    CAMLparam1 (config);

    wifi_sta_config_t sta_config;

    /* copy ssid */
    int len = caml_string_length(Field(config, 0));
    if (len > 32) {
        CAMLreturn (result_fail(0));
    }
    memcpy(&sta_config.ssid, Bytes_val(Field(config, 0)), len+1);

    /* copy password */
    len = caml_string_length(Field(config, 1));
    if (len > 64) {
        CAMLreturn (result_fail(0));
    }
    memcpy(&sta_config.password, Bytes_val(Field(config, 1)), len+1);

    sta_config.bssid_set = false;

    wifi_config_t esp_config = {
        .sta = sta_config
    };

    if (esp_wifi_set_config(WIFI_IF_STA, &esp_config) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_sta_get_config(value unit) {
    CAMLparam0 ();
    CAMLlocal1 (ml_config);

    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }
    ml_config = caml_alloc_tuple(2);

    /* Copy ssid */
    int len = strlen(config.sta.ssid);
    Store_field(ml_config, 0, 
                caml_alloc_initialized_string(len, config.sta.ssid));
    /* Copy password */
    len = strlen(config.sta.password);
    Store_field(ml_config, 1, 
                caml_alloc_initialized_string(len, config.sta.password));

    CAMLreturn (result_ok(ml_config));
}

CAMLprim 
value ml_wifi_connect(value unit) {
    CAMLparam0 ();

    if (esp_wifi_connect() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_disconnect(value unit) {
    CAMLparam0 ();

    if (esp_wifi_disconnect() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_start(value unit) {
    CAMLparam0 ();

    wifi_scan_config_t scan_config;
    if (esp_wifi_scan_start(&scan_config, false) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_stop(value unit) {
    CAMLparam0 ();

    if (esp_wifi_scan_stop() != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_scan_count(value unit) {
    CAMLparam0 ();

    uint16_t ap_count;

    if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(Val_int(ap_count)));
}

value write_ap_description(wifi_ap_record_t* record) {
    CAMLparam0 ();
    CAMLlocal1 (result);
    result = caml_alloc_tuple(3);
    
    /* Copy bssid */
    Store_field(result, 0, 
                caml_alloc_initialized_string(6, record->bssid));
    /* Copy ssid */
    int len = strlen(record->ssid);
    Store_field(result, 1, 
                caml_alloc_initialized_string(len, record->ssid));

    value ml_auth_mode;
    switch (record->authmode) {
        case WIFI_AUTH_OPEN:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_OPEN);
            break;
        case WIFI_AUTH_WPA_PSK:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA_PSK);
            break;
        case WIFI_AUTH_WPA2_PSK:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA2_PSK);
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA_WPA2_PSK);
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            ml_auth_mode = Val_int(ML_WIFI_AUTH_WPA2_ENTERPRISE);
            break;
    }
    Store_field(result, 2, ml_auth_mode);
    return result;
}

CAMLprim 
value ml_wifi_scan_get_array(value v_count) {
    CAMLparam1 (v_count);
    CAMLlocal1 (result);
    uint16_t count = Int_val(v_count);
    wifi_ap_record_t* ap_records = malloc(sizeof(wifi_ap_record_t) * count);

    if (esp_wifi_scan_get_ap_records(&count, ap_records) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    result = caml_alloc_tuple(count);
    for (int i=0;i<count;i++) {
        Store_field(result, i, write_ap_description(&ap_records[i]));
    }

    CAMLreturn (result_ok(result));
}

CAMLprim 
value ml_wifi_read(value v_interface, value v_buffer, value v_buffer_size) {
    CAMLparam3 (v_interface, v_buffer, v_buffer_size);

    wifi_interface_t interface;
    switch (v_interface) {
        case ML_WIFI_IF_AP:
            interface = WIFI_IF_AP;
            break;
        case ML_WIFI_IF_STA:
            interface = WIFI_IF_STA;
            break;
    }
    uint8_t* buf    = Caml_ba_data_val(v_buffer);
    size_t size     = Long_val(v_buffer_size);

    int error_code = wifi_read(interface, buf, &size);

    if (error_code != WIFI_ERR_OK) {
        switch (error_code) {
            case WIFI_ERR_AGAIN:
                CAMLreturn (result_fail(ML_WIFI_ERROR_NOTHING_TO_READ));
                break;
            case WIFI_ERR_INVAL:
                CAMLreturn (result_fail(ML_WIFI_ERROR_INVALID_ARGUMENT));
                break;
            default:
                CAMLreturn (result_fail(ML_WIFI_ERROR_UNSPECIFIED));
                break;
        }
    }

    CAMLreturn (result_ok(Val_int(size)));
}

CAMLprim 
value ml_wifi_write(value v_interface, value v_buffer, value v_buffer_size) {
    CAMLparam3 (v_interface, v_buffer, v_buffer_size);

    wifi_interface_t interface;
    switch (v_interface) {
        case ML_WIFI_IF_AP:
            interface = WIFI_IF_AP;
            break;
        case ML_WIFI_IF_STA:
            interface = WIFI_IF_STA;
            break;
    }
    uint8_t* buf    = Caml_ba_data_val(v_buffer);
    size_t size     = Long_val(v_buffer_size);

    int error_code = wifi_write(interface, buf, &size);
    
    if (error_code != WIFI_ERR_OK) {
        switch (error_code) {
            case WIFI_ERR_INVAL:
                CAMLreturn (result_fail(ML_WIFI_ERROR_INVALID_ARGUMENT));
                break;
            default:
                CAMLreturn (result_fail(ML_WIFI_ERROR_UNSPECIFIED));
                break;
        }
    }
    CAMLreturn (result_ok(Val_unit));
}

CAMLprim 
value ml_wifi_get_mac(value v_interface) {
    CAMLparam1 (v_interface);
    CAMLlocal1 (v_result);
    
    wifi_interface_t interface;
    switch (v_interface) {
        case ML_WIFI_IF_AP:
            interface = WIFI_IF_AP;
            break;
        case ML_WIFI_IF_STA:
            interface = WIFI_IF_STA;
            break;
    }
    v_result = caml_alloc_string(6);

    if (esp_wifi_get_mac(interface, Bytes_val(v_result)) != ESP_OK) {
        CAMLreturn (result_fail(0));
    }

    CAMLreturn (result_ok(v_result));
}

CAMLprim
value ml_wifi_get_status(value unit) {
    CAMLparam0 ();
    CAMLlocal1 (v_result); 

    wifi_status st = wifi_get_status();
    v_result = caml_alloc_tuple(4);
    Store_field(v_result, 0, Val_bool(st.wifi_inited));
    Store_field(v_result, 1, Val_bool(st.ap_started));
    Store_field(v_result, 2, Val_bool(st.sta_started));
    Store_field(v_result, 3, Val_bool(st.sta_connected));

    CAMLreturn (v_result);
}