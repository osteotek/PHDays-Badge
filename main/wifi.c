#include "dhcpserver/dhcpserver.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "home_wifi.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "wifi.h"

#define ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN
#define DEFAULT_RSSI -127
#define DEFAULT_RSSI_5G_ADJUSTMENT 0


// With home Wi-Fi configured the badge runs as a station only. The hotspot is a
// fallback: it starts when home Wi-Fi has no IP for HOTSPOT_FALLBACK_DELAY_US and
// stops once home Wi-Fi connects again.
#define HOTSPOT_FALLBACK_DELAY_US (120 * 1000000LL)
#define RECONNECT_DELAY_US (5 * 1000000LL)
// Each connection attempt scans all channels and briefly disrupts hotspot
// clients, so retry less often while someone may be using the hotspot.
#define RECONNECT_DELAY_HOTSPOT_US (30 * 1000000LL)

static const char *TAG = "wifi";
static bool home_wifi_configured;
static bool hotspot_active;
static bool home_connected;
static esp_netif_t *sta_netif;
static esp_timer_handle_t reconnect_timer;
static esp_timer_handle_t fallback_timer;
static wifi_config_t wifi_config_ap;

static unsigned char ESP_WIFI_SSID[16];

static void schedule_reconnect(void) {
    if (home_wifi_configured && !esp_timer_is_active(reconnect_timer))
        esp_timer_start_once(reconnect_timer, hotspot_active ? RECONNECT_DELAY_HOTSPOT_US : RECONNECT_DELAY_US);
}

static void connect_home_wifi(void *arg) {
    if (!home_wifi_configured)
        return;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connection attempt failed: %s", esp_err_to_name(err));
        schedule_reconnect();
    }
}

static esp_err_t start_hotspot(void) {
    if (hotspot_active)
        return ESP_OK;
    esp_err_t err = esp_wifi_set_mode(home_wifi_configured ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err == ESP_OK)
        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap);
    if (err == ESP_OK)
        err = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (err == ESP_OK)
        err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW40);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start hotspot: %s", esp_err_to_name(err));
        return err;
    }
    hotspot_active = true;
    ESP_LOGI(TAG, "Hotspot on. SSID:%s; Wi-Fi setup: http://192.168.4.1/wifi", ESP_WIFI_SSID);
    return ESP_OK;
}

static void stop_hotspot(void) {
    if (!hotspot_active)
        return;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not stop hotspot: %s", esp_err_to_name(err));
        return;
    }
    hotspot_active = false;
    ESP_LOGI(TAG, "Hotspot off; home Wi-Fi connected");
}

static void hotspot_fallback(void *arg) {
    if (home_connected)
        return;
    ESP_LOGW(TAG, "Home Wi-Fi unavailable for %lld s; starting hotspot", HOTSPOT_FALLBACK_DELAY_US / 1000000);
    start_hotspot();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "sta started");
        connect_home_wifi(NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = event_data;
        home_connected = false;
        ESP_LOGW(TAG, "Home Wi-Fi disconnected (reason %u); retrying in %d seconds", event->reason, hotspot_active ? 30 : 5);
        schedule_reconnect();
        if (home_wifi_configured && !hotspot_active && !esp_timer_is_active(fallback_timer))
            esp_timer_start_once(fallback_timer, HOTSPOT_FALLBACK_DELAY_US);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "sta scan done");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "sta connected");
        // A link-local IPv6 address lets mDNS answer AAAA queries; without one,
        // macOS waits about 5 s for each pixeldesk.local lookup.
        esp_netif_create_ip6_linklocal(sta_netif);
        esp_timer_stop(reconnect_timer);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Home Wi-Fi ready. Open http://" IPSTR "/", IP2STR(&event->ip_info.ip));
        home_connected = true;
        esp_timer_stop(fallback_timer);
        stop_hotspot();
    }
}

// Hotspot name: "pixeldesk-" and the last two MAC bytes, so nearby badges differ.
static void set_hotspot_name(void) {
    uint8_t mac[6] = {0};
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Could not read the MAC address: %s", esp_err_to_name(err));
    snprintf((char *)ESP_WIFI_SSID, sizeof(ESP_WIFI_SSID), "pixeldesk-%02x%02x", mac[4], mac[5]);
}

void wifi_init_softap(void) {

    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    // ESP-IDF 6 no longer offers the AP address as DNS by default
    // (CONFIG_LWIP_DHCPS_ADD_DNS was removed). Hotspot clients must use the
    // badge's captive-portal DNS server, so advertise it explicitly.
    esp_netif_ip_info_t ap_ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap, &ap_ip));
    esp_netif_dns_info_t ap_dns = {.ip = {.type = ESP_IPADDR_TYPE_V4, .u_addr.ip4 = ap_ip.ip}};
    dhcps_offer_t offer_dns = OFFER_DNS;
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &ap_dns));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));

    esp_netif_t *sta = sta_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta, BADGE_HOSTNAME));

    const esp_timer_create_args_t reconnect_args = {.callback = connect_home_wifi, .name = "wifi_reconnect"};
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_args, &reconnect_timer));
    const esp_timer_create_args_t fallback_args = {.callback = hotspot_fallback, .name = "hotspot_fallback"};
    ESP_ERROR_CHECK(esp_timer_create(&fallback_args, &fallback_timer));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_ap = (wifi_config_t){
        .ap =
            {
                .ssid_len = strlen((const char *)ESP_WIFI_SSID),
                .channel = ESP_WIFI_CHANNEL,
                .max_connection = MAX_STA_CONN,
                // Open: the hotspot only runs while home Wi-Fi is unreachable,
                // so that another network can be entered from any phone.
                .authmode = WIFI_AUTH_OPEN,
            },
    };
    wifi_config_t wifi_config_sta = {
        .sta =
            {
                .scan_method = WIFI_FAST_SCAN,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .threshold.rssi = DEFAULT_RSSI,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .threshold.rssi_5g_adjustment = DEFAULT_RSSI_5G_ADJUSTMENT,
            },
    };
    home_wifi_configured = loadHomeWiFi(&wifi_config_sta);

    memcpy(wifi_config_ap.ap.ssid, ESP_WIFI_SSID, strlen((const char *)ESP_WIFI_SSID));

    if (home_wifi_configured) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20));
    } else {
        ESP_ERROR_CHECK(start_hotspot());
    }
    // Use normal adaptive transmit rates/power for reliable home-network range.
    ESP_ERROR_CHECK(esp_wifi_start());

    if (home_wifi_configured) {
        ESP_LOGI(TAG, "Joining home Wi-Fi; hotspot starts if it is unavailable for %lld s", HOTSPOT_FALLBACK_DELAY_US / 1000000);
        ESP_ERROR_CHECK(esp_timer_start_once(fallback_timer, HOTSPOT_FALLBACK_DELAY_US));
    } else {
        ESP_LOGI(TAG, "No home Wi-Fi configured; hotspot only");
    }
}

bool wifi_home_configured(void) { return home_wifi_configured; }

bool wifi_home_connected(void) { return home_connected; }

bool wifi_hotspot_active(void) { return hotspot_active; }

// Advertises pixeldesk.local and the web UI on every active interface.
static void start_mdns(void) {
    esp_err_t err = mdns_init();
    if (err == ESP_OK)
        err = mdns_hostname_set(BADGE_HOSTNAME);
    if (err == ESP_OK)
        err = mdns_instance_name_set("Pixeldesk");
    if (err == ESP_OK)
        err = mdns_service_add("Pixeldesk", "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "mDNS failed: %s", esp_err_to_name(err));
}

void initWiFi(void) {
    set_hotspot_name();
    wifi_init_softap();
    start_mdns();
}
