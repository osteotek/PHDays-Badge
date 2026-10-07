#include "home_wifi.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "home_wifi_secrets.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// One versioned NVS blob keeps the SSID/password pair together. Credentials are
// never logged or returned by the HTTP API. Settings saved through the hotspot
// page take precedence over the defaults compiled in from wifi_secrets.ini.
typedef struct {
    char ssid[33];
    char password[65];
} home_credentials_t;

static const char *TAG = "home_wifi";
static esp_timer_handle_t restart_timer;

static bool valid_credentials(const char *ssid, const char *password) {
    size_t ssid_len = strlen(ssid), password_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32 || password_len < 8 || password_len > 64)
        return false;
    if (password_len == 64) {
        for (size_t i = 0; i < password_len; i++) {
            if (!isxdigit((unsigned char)password[i]))
                return false;
        }
    }
    return true;
}

static bool load_saved_credentials(home_credentials_t *credentials) {
    nvs_handle_t handle;
    if (nvs_open("home_wifi", NVS_READONLY, &handle) != ESP_OK)
        return false;
    size_t length = sizeof(*credentials);
    esp_err_t err = nvs_get_blob(handle, "credentials_v1", credentials, &length);
    nvs_close(handle);
    return err == ESP_OK && length == sizeof(*credentials) && credentials->ssid[32] == '\0' && credentials->password[64] == '\0' &&
           valid_credentials(credentials->ssid, credentials->password);
}

static bool load_build_credentials(home_credentials_t *credentials) {
    _Static_assert(sizeof(HOME_WIFI_BUILD_SSID) <= sizeof(credentials->ssid), "wifi_secrets.ini ssid too long");
    _Static_assert(sizeof(HOME_WIFI_BUILD_PASSWORD) <= sizeof(credentials->password), "wifi_secrets.ini password too long");
    memset(credentials, 0, sizeof(*credentials));
    memcpy(credentials->ssid, HOME_WIFI_BUILD_SSID, sizeof(HOME_WIFI_BUILD_SSID));
    memcpy(credentials->password, HOME_WIFI_BUILD_PASSWORD, sizeof(HOME_WIFI_BUILD_PASSWORD));
    return valid_credentials(credentials->ssid, credentials->password);
}

bool loadHomeWiFi(wifi_config_t *config) {
    home_credentials_t credentials = {0};
    if (load_saved_credentials(&credentials)) {
        ESP_LOGI(TAG, "Using home Wi-Fi saved on the badge");
    } else if (load_build_credentials(&credentials)) {
        ESP_LOGI(TAG, "Using home Wi-Fi from wifi_secrets.ini");
    } else {
        memset(&credentials, 0, sizeof(credentials));
        return false;
    }
    memcpy(config->sta.ssid, credentials.ssid, strlen(credentials.ssid));
    memcpy(config->sta.password, credentials.password, strlen(credentials.password));
    memset(&credentials, 0, sizeof(credentials));
    return true;
}

static bool request_on_hotspot(httpd_req_t *req) {
    struct sockaddr_storage address = {0};
    socklen_t length = sizeof(address);
    esp_netif_ip_info_t ap_ip;
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap || esp_netif_get_ip_info(ap, &ap_ip) != ESP_OK ||
        getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&address, &length) != 0)
        return false;
    if (address.ss_family == AF_INET)
        return ((struct sockaddr_in *)&address)->sin_addr.s_addr == ap_ip.ip.addr;
#if CONFIG_LWIP_IPV6
    // ESP-IDF's HTTP server uses a dual-stack socket when IPv6 is enabled.
    if (address.ss_family == AF_INET6) {
        const struct in6_addr *ip = &((struct sockaddr_in6 *)&address)->sin6_addr;
        uint32_t ipv4;
        memcpy(&ipv4, &ip->s6_addr[12], sizeof(ipv4));
        return IN6_IS_ADDR_V4MAPPED(ip) && ipv4 == ap_ip.ip.addr;
    }
#endif
    return false;
}

static void restart_badge(void *arg) { esp_restart(); }

static esp_err_t settings_page(httpd_req_t *req) {
    extern const uint8_t page_start[] asm("_binary_wifi_html_start");
    extern const uint8_t page_end[] asm("_binary_wifi_html_end");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start);
}

static esp_err_t settings_status(httpd_req_t *req) {
    wifi_config_t config = {0};
    esp_wifi_get_config(WIFI_IF_STA, &config);
    char ssid[33] = {0};
    memcpy(ssid, config.sta.ssid, 32);
    memset(config.sta.password, 0, sizeof(config.sta.password));
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {0};
    bool connected = sta && esp_netif_is_netif_up(sta) && esp_netif_get_ip_info(sta, &info) == ESP_OK && info.ip.addr != 0;
    char ip[16] = "";
    if (connected)
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    cJSON *response = cJSON_CreateObject();
    if (!response)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    cJSON_AddStringToObject(response, "ssid", ssid);
    cJSON_AddBoolToObject(response, "connected", connected);
    cJSON_AddStringToObject(response, "ip", ip);
    cJSON_AddBoolToObject(response, "can_configure", request_on_hotspot(req));
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!json)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return err;
}

static esp_err_t settings_save(httpd_req_t *req) {
    // Configuration is available only through the password-protected hotspot.
    // JSON-only requests, without CORS, also prevent cross-site form submissions.
    if (!request_on_hotspot(req))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Connect to the badge hotspot to change Wi-Fi settings");
    char content_type[48];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") != 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected application/json");
    if (esp_timer_is_active(restart_timer))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Restart already scheduled");
    char body[1024];
    if (req->content_len == 0 || req->content_len >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid settings length");
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete settings");
        received += count;
    }
    body[received] = '\0';
    cJSON *root = cJSON_Parse(body);
    memset(body, 0, sizeof(body));
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) || !valid_credentials(ssid->valuestring, password->valuestring)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Use an SSID of 1-32 bytes and a WPA2 password of 8-63 bytes (or 64 hex digits)");
    }
    home_credentials_t credentials = {0};
    strcpy(credentials.ssid, ssid->valuestring);
    strcpy(credentials.password, password->valuestring);
    memset(password->valuestring, 0, strlen(password->valuestring));
    cJSON_Delete(root);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("home_wifi", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "credentials_v1", &credentials, sizeof(credentials));
        if (err == ESP_OK)
            err = nvs_commit(handle);
        nvs_close(handle);
    }
    memset(&credentials, 0, sizeof(credentials));
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save Wi-Fi settings");
    err = esp_timer_start_once(restart_timer, 2000000);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Settings saved; restart the badge manually");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"saved\":true,\"restarting\":true}");
}

esp_err_t registerHomeWiFiSettings(httpd_handle_t server) {
    const esp_timer_create_args_t timer_args = {.callback = restart_badge, .name = "wifi_restart"};
    esp_err_t err = esp_timer_create(&timer_args, &restart_timer);
    if (err != ESP_OK)
        return err;
    const httpd_uri_t routes[] = {
        {.uri = "/wifi", .method = HTTP_GET, .handler = settings_page},
        {.uri = "/api/v1/wifi", .method = HTTP_GET, .handler = settings_status},
        {.uri = "/api/v1/wifi", .method = HTTP_POST, .handler = settings_save},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK)
            return err;
    }
    return ESP_OK;
}
