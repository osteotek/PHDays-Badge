#include "clock_time.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include <time.h>

static const char *TAG = "clock";

// Started on every new station IP: a request sent before the network is up
// fails, and lwIP's SNTP client then backs off for a long time.
static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_err_t err = esp_netif_sntp_start();
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Starting NTP failed: %s", esp_err_to_name(err));
}

#define NTP_SERVERS "pool.ntp.org", "time.google.com", "time.cloudflare.com"
static const char *const servers[] = {NTP_SERVERS};
#define NTP_SERVER_COUNT (sizeof(servers) / sizeof(servers[0]))

void clock_time_start(void) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(NTP_SERVER_COUNT, ESP_SNTP_SERVER_LIST(NTP_SERVERS));
    config.start = false;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK)
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Setting up NTP failed: %s", esp_err_to_name(err));
}

cJSON *clock_time_ntp_json(void) {
    cJSON *list = cJSON_CreateArray();
    for (unsigned i = 0; i < NTP_SERVER_COUNT; i++) {
        unsigned int reachability = 0;
        esp_netif_sntp_reachability(i, &reachability);
        cJSON *server = cJSON_CreateObject();
        cJSON_AddStringToObject(server, "server", servers[i]);
        cJSON_AddNumberToObject(server, "reachability", reachability); // bit per recent request
        cJSON_AddItemToArray(list, server);
    }
    return list;
}

bool clock_time_get(screen_time_t *out) {
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    // Before the first NTP sync the clock counts up from 1970.
    out->valid = local.tm_year + 1900 >= 2025;
    out->hour = local.tm_hour;
    out->minute = local.tm_min;
    out->second = local.tm_sec;
    return out->valid;
}
