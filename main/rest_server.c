#include "rest_server.h"

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_vfs.h"

#include "buzzer.h"
#include "home_wifi.h"
#include "info_api.h"
#include "ota_upload.h"

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_LITTLEFS_OBJ_NAME_LEN)
#define MAX_FILE_SIZE (250 * 1024)
#define MAX_FILE_SIZE_STR "250KB"
#define SCRATCH_BUFSIZE 4096
// httpd waits 5 s per receive; give up on a silent client after a few tries
// instead of blocking the single server task forever.
#define RECV_TIMEOUT_RETRIES 3
#define MELODIES_FILE "/melodies_list.json"
#define LEGACY_PROJECTS_FILE "/projects.json" // pixel editor projects, no longer used

// Served until melodies are saved from the web page.
static const char *const DEFAULT_MELODIES[] = {
    "Alert:d=8,o=6,b=180:c,e,g,c7",
    "A-Team:d=8,o=5,b=125:4d#6,a#,2d#6,16p,g#,4a#,4d#.,p,16g,16a#,d#6,a#,f6,2d#6,16p,c#.6,16c6,16a#,g#.,2a#",
    "The Simpsons:d=4,o=5,b=160:c.6,e6,f#6,8a6,g.6,e6,c6,8a,8f#,8f#,8f#,2g,8p,8p,8f#,8f#,8f#,8g,a#.,8c6,8c6,8c6,c6",
    "Indiana Jones:d=4,o=5,b=250:e,8p,8f,8g,8p,1c6,8p.,d,8p,8e,1f,p.,g,8p,8a,8b,8p,1f6,p,a,8p,8b,2c6,2d6,2e6,e,8p,8f,8g,8p,1c6,p,d6,8p,8e6,1f.6",
    "James Bond:d=4,o=5,b=320:c,8d,8d,d,2d,c,c,c,c,8d#,8d#,2d#,d,d,d,c,8d,8d,d,2d,c,c,c,c,8d#,8d#,d#,2d#,d,c#,c,c6,1b.,g,f,1g.",
};

struct file_server_data {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
};

#define REQ_CTX ((struct file_server_data *)(req->user_ctx))

static const char *TAG = "file_server";

// Receives exactly len bytes of the request body into buf.
static esp_err_t recv_exact(httpd_req_t *req, char *buf, size_t len) {
    size_t received = 0;
    int timeouts = 0;
    while (received < len) {
        int count = httpd_req_recv(req, buf + received, len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= RECV_TIMEOUT_RETRIES)
            continue;
        if (count <= 0)
            return ESP_FAIL;
        received += count;
        timeouts = 0;
    }
    return ESP_OK;
}

esp_err_t http_recv_body(httpd_req_t *req, char *buf, size_t size) {
    if (req->content_len >= size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }
    if (recv_exact(req, buf, req->content_len) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Failed to receive request");
        return ESP_FAIL;
    }
    buf[req->content_len] = '\0';
    return ESP_OK;
}

esp_err_t http_send_json(httpd_req_t *req, cJSON *json) {
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return err;
}

// Streams the request body into path via a temporary file that replaces path
// only once complete, so a failed save keeps the previous file.
static esp_err_t recv_body_to_file(httpd_req_t *req, const char *path) {
    char tmp_path[FILE_PATH_MAX + sizeof(".tmp")];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE *fd = fopen(tmp_path, "w");
    if (!fd) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }
    char *buf = REQ_CTX->scratch;
    const char *error = NULL;
    for (size_t remaining = req->content_len; remaining > 0 && !error;) {
        size_t len = MIN(remaining, SCRATCH_BUFSIZE);
        if (recv_exact(req, buf, len) != ESP_OK)
            error = "Failed to receive file";
        else if (fwrite(buf, 1, len, fd) != len)
            error = "Failed to write file to storage";
        else
            remaining -= len;
    }
    if (fclose(fd) != 0 && !error)
        error = "Failed to write file to storage";
    if (!error && rename(tmp_path, path) != 0)
        error = "Failed to write file to storage";
    if (error) {
        unlink(tmp_path);
        ESP_LOGE(TAG, "%s: %s", error, path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// The web UI is a single gzipped page built from webui/.
static esp_err_t index_html_get_handler(httpd_req_t *req) {
    extern const uint8_t index_start[] asm("_binary_index_html_gz_start");
    extern const uint8_t index_end[] asm("_binary_index_html_gz_end");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_start, index_end - index_start);
}

static esp_err_t buzzer_melody_post_handler(httpd_req_t *req) {
    char *buf = REQ_CTX->scratch;
    if (http_recv_body(req, buf, SCRATCH_BUFSIZE) != ESP_OK)
        return ESP_FAIL;
    cJSON *root = cJSON_Parse(buf);
    const char *melody = cJSON_GetStringValue(cJSON_GetObjectItem(root, "melody"));
    if (melody == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected {\"melody\": \"<RTTTL>\"}");
    }
    int status = parse_rtttl(melody, strlen(melody));
    cJSON_Delete(root);
    if (status < 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a playable RTTTL melody");
    return httpd_resp_sendstr(req, "Playing");
}

static esp_err_t melodies_list_get_handler(httpd_req_t *req) {
    char path[FILE_PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", REQ_CTX->base_path, MELODIES_FILE);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    FILE *fd = fopen(path, "r");
    if (!fd) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddItemToObject(json, "melodies", cJSON_CreateStringArray(DEFAULT_MELODIES, sizeof(DEFAULT_MELODIES) / sizeof(DEFAULT_MELODIES[0])));
        return http_send_json(req, json);
    }
    char *chunk = REQ_CTX->scratch;
    size_t size = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
    // Lists saved by the old editor are gzipped; let the browser inflate them.
    if (size >= 2 && (uint8_t)chunk[0] == 0x1f && (uint8_t)chunk[1] == 0x8b)
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    esp_err_t err = ESP_OK;
    while (size > 0 && err == ESP_OK) {
        err = httpd_resp_send_chunk(req, chunk, size);
        size = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
    }
    fclose(fd);
    if (err != ESP_OK)
        return err;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t melodies_list_post_handler(httpd_req_t *req) {
    if (req->content_len > MAX_FILE_SIZE)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File size must be less than " MAX_FILE_SIZE_STR);
    char path[FILE_PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", REQ_CTX->base_path, MELODIES_FILE);
    if (recv_body_to_file(req, path) != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_sendstr(req, "Saved");
}

// Captive portal: everything unknown goes to the web UI.
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal.
    return httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);
}

esp_err_t start_file_server(const char *base_path) {
    static struct file_server_data *server_data = NULL;
    if (server_data)
        return ESP_ERR_INVALID_STATE;
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data)
        return ESP_ERR_NO_MEM;
    strlcpy(server_data->base_path, base_path, sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;
    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK)
        return ESP_FAIL;

    ESP_ERROR_CHECK(registerHomeWiFiSettings(server));
    ESP_ERROR_CHECK(registerOtaUpload(server));
    ESP_ERROR_CHECK(registerInfoApi(server));
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_html_get_handler},
        {.uri = "/api/v1/buzzer/melody", .method = HTTP_POST, .handler = buzzer_melody_post_handler, .user_ctx = server_data},
        {.uri = "/api/v1/buzzer/melodies", .method = HTTP_GET, .handler = melodies_list_get_handler, .user_ctx = server_data},
        {.uri = "/api/v1/buzzer/melodies", .method = HTTP_POST, .handler = melodies_list_post_handler, .user_ctx = server_data},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    return ESP_OK;
}

esp_err_t mount_storage(const char *base_path) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = "storage",
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount storage (%s)", esp_err_to_name(ret));
        return ret;
    }
    char legacy[ESP_VFS_PATH_MAX + sizeof(LEGACY_PROJECTS_FILE)];
    snprintf(legacy, sizeof(legacy), "%s%s", base_path, LEGACY_PROJECTS_FILE);
    if (unlink(legacy) == 0)
        ESP_LOGI(TAG, "Removed %s", LEGACY_PROJECTS_FILE);
    size_t total = 0, used = 0;
    if (esp_littlefs_info("storage", &total, &used) == ESP_OK)
        ESP_LOGI(TAG, "Storage: %u of %u bytes used", (unsigned)used, (unsigned)total);
    return ESP_OK;
}
