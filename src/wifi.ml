(* Wifi mode *)
type wifi_mode = MODE_STA | MODE_AP | MODE_APSTA

(* Wifi interface *)
type wifi_interface = IF_STA | IF_AP

type wifi_auth_mode = 
    | AUTH_OPEN
    | AUTH_WPA_PSK
    | AUTH_WPA2_PSK
    | AUTH_WPA_WPA2_PSK
    | AUTH_WPA2_ENTERPRISE

type wifi_status = {
    inited: bool;
    ap_started: bool;
    sta_started: bool;
    sta_connected: bool;    
}

type wifi_configuration_ap = {
    ssid: Bytes.t;
    password: Bytes.t;
    channel: int;
    auth_mode: wifi_auth_mode;
    ssid_hidden: bool;
    max_connection: int;
    beacon_interval: int;
}

type wifi_configuration_sta = {
    ssid: Bytes.t;
    password: Bytes.t;
}

type wifi_error = 
    | Unspecified
    | Invalid_argument
    | Out_of_memory
    | Nothing_to_read
    | Wifi_not_inited

type wifi_sta_description = {
    mac: Bytes.t;
}

type wifi_ap_description = {
    bssid: Bytes.t;
    ssid: Bytes.t;
    auth_mode: wifi_auth_mode;
}


external get_status : unit -> wifi_status = "ml_wifi_get_status"

external initialize : unit -> (unit, wifi_error) result = "ml_wifi_initialize"
external deinitialize : unit -> (unit, wifi_error) result = "ml_wifi_deinitialize"

external set_mode : wifi_mode -> (unit, wifi_error) result = "ml_wifi_set_mode"
external get_mode : unit -> (wifi_mode, wifi_error) result = "ml_wifi_get_mode"

external start : unit -> (unit, wifi_error) result = "ml_wifi_start"
external stop : unit -> (unit, wifi_error) result = "ml_wifi_stop"

external ap_set_config : wifi_configuration_ap -> (unit, wifi_error) result = "ml_wifi_ap_set_config"
external ap_get_config : unit -> (wifi_configuration_ap, wifi_error) result = "ml_wifi_ap_get_config"

external sta_set_config : wifi_configuration_sta -> (unit, wifi_error) result = "ml_wifi_sta_set_config"
external sta_get_config : unit -> (wifi_configuration_sta, wifi_error) result = "ml_wifi_sta_get_config"

external connect : unit -> (unit, wifi_error) result = "ml_wifi_connect"
external disconnect : unit -> (unit, wifi_error) result = "ml_wifi_disconnect"

(* Scanning functions *)

external scan_start : unit -> (unit, wifi_error) result = "ml_wifi_scan_start"
external scan_stop : unit -> (unit, wifi_error) result = "ml_wifi_scan_stop"
external scan_count : unit -> (int, wifi_error) result = "ml_wifi_scan_count"
external scan_get_list : int -> (wifi_ap_description list, wifi_error) result = "ml_wifi_scan_get_list"

(* Network interface functions *)

external read : wifi_interface -> Cstruct.buffer -> int -> (int, wifi_error) result = "ml_wifi_read"
external write : wifi_interface -> Cstruct.buffer -> int -> (unit, wifi_error) result = "ml_wifi_write"
external get_mac : wifi_interface -> Bytes.t = "ml_wifi_get_mac"

