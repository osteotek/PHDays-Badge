
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_err.h"
#include "esp_log.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_random.h"
#include "esp_vfs.h"

#include "buzzer.h"
#include "home_wifi.h"
#include "led_plate.h"
#include "ota_upload.h"
#include "rest_server.h"

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_LITTLEFS_OBJ_NAME_LEN)

#define MAX_FILE_SIZE (250 * 1024)
#define MAX_FILE_SIZE_STR "250KB"

/* Scratch buffer size */
// Size: 8192
//  Size: 16384
// Size: 24576
// Size: 32768
// Size: 40960
// Size: 49152
// Size: 57344
// 16 KB: an 8-frame animation as JSON ("255," per channel) is about 9.6 KB.
#define SCRATCH_BUFSIZE 16384
// httpd waits 5 s per receive; give up on a silent client after a few tries
// instead of blocking the single server task forever.
#define RECV_TIMEOUT_RETRIES 3

#define REQ_CTX ((struct file_server_data *)(req->user_ctx))

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "file_server";

uint8_t webOpenedCounter = 0;
uint8_t imageSetCounter = 0;
uint8_t projectSaveCounter = 0;

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

// Reads a JSON request body into the scratch buffer as a NUL-terminated string.
// Sends the error response itself on failure.
static esp_err_t recv_json_body(httpd_req_t *req, char *buf) {
    if (req->content_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    if (recv_exact(req, buf, req->content_len) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive request");
        return ESP_FAIL;
    }
    buf[req->content_len] = '\0';
    return ESP_OK;
}

// Streams the request body into path. With atomic set it is written to a
// temporary file that replaces path only once complete, so a failed or aborted
// save keeps the previous file. Sends the error response itself on failure.
static esp_err_t recv_body_to_file(httpd_req_t *req, const char *path, bool atomic) {
    char tmp_path[FILE_PATH_MAX + sizeof(".tmp")];
    const char *write_path = path;
    if (atomic) {
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
        write_path = tmp_path;
    }
    FILE *fd = fopen(write_path, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file : %s", write_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Receiving file : %s...", path);
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
    if (!error && atomic && rename(tmp_path, path) != 0)
        error = "Failed to write file to storage";
    if (error) {
        unlink(write_path);
        ESP_LOGE(TAG, "%s: %s", error, path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "File reception complete");
    return ESP_OK;
}

// JSON number clamped to a colour channel; anything else counts as 0.
static uint8_t json_byte(const cJSON *item) {
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0)
        return 0;
    return item->valuedouble >= 255 ? 255 : (uint8_t)item->valuedouble;
}

// Validates {"frames":[{"frame":[r,g,b,...],"delay":ms},...]} into image.
// Returns an error message, or NULL on success. Short frames are padded black.
static const char *parse_picture(const cJSON *root, Image *image) {
    const cJSON *frames = cJSON_GetObjectItem(root, "frames");
    int count = cJSON_GetArraySize(frames);
    if (!cJSON_IsArray(frames) || count < 1 || count > IMAGE_MAX_FRAMES)
        return "Picture needs 1-8 frames";
    memset(image, 0, sizeof(*image));
    image->framesCount = count;
    size_t n = 0;
    const cJSON *frame;
    cJSON_ArrayForEach(frame, frames) {
        const cJSON *values = cJSON_GetObjectItem(frame, "frame");
        const cJSON *delay = cJSON_GetObjectItem(frame, "delay");
        if (!cJSON_IsArray(values) || cJSON_GetArraySize(values) > 300)
            return "Each frame needs a \"frame\" array of up to 300 values";
        double ms = cJSON_IsNumber(delay) ? delay->valuedouble : 1000;
        image->frames[n].duration = ms <= 0 ? 0 : ms >= UINT16_MAX ? UINT16_MAX : (uint16_t)ms;
        size_t i = 0;
        const cJSON *value;
        cJSON_ArrayForEach(value, values) {
            Pixel *pixel = &image->frames[n].pixels[i / 3];
            uint8_t *channel = i % 3 == 0 ? &pixel->r : i % 3 == 1 ? &pixel->g : &pixel->b;
            *channel = json_byte(value);
            i++;
        }
        n++;
    }
    return NULL;
}

static esp_err_t index_html_get_handler(httpd_req_t *req) {
    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_size);
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
}

static esp_err_t main_css_get_handler(httpd_req_t *req) {
    extern const uint8_t main_css_start[] asm("_binary_main_css_gz_start");
    extern const uint8_t main_css_end[] asm("_binary_main_css_gz_end");
    const size_t main_css_size = (main_css_end - main_css_start);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)main_css_start, main_css_size);
}

static esp_err_t main_js_get_handler(httpd_req_t *req) {
    extern const uint8_t main_js_start[] asm("_binary_main_js_gz_start");
    extern const uint8_t main_js_end[] asm("_binary_main_js_gz_end");
    const size_t main_js_size = (main_js_end - main_js_start);
    httpd_resp_set_type(req, "text/javascript ");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)main_js_start, main_js_size);
}

static esp_err_t apple_touch_icon_get_handler(httpd_req_t *req) {
    extern const uint8_t apple_touch_icon_png_start[] asm("_binary_apple_touch_icon_png_start");
    extern const uint8_t apple_touch_icon_png_end[] asm("_binary_apple_touch_icon_png_end");
    const size_t apple_touch_icon_png_size = (apple_touch_icon_png_end - apple_touch_icon_png_start);
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)apple_touch_icon_png_start, apple_touch_icon_png_size);
}

static esp_err_t ch_1_js_get_handler(httpd_req_t *req) {
    extern const uint8_t ch_1_js_start[] asm("_binary_1_js_gz_start");
    extern const uint8_t ch_1_js_end[] asm("_binary_1_js_gz_end");
    const size_t ch_1_js_size = (ch_1_js_end - ch_1_js_start);
    httpd_resp_set_type(req, "text/javascript ");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)ch_1_js_start, ch_1_js_size);
}

static esp_err_t epilepsy_sans_eot_get_handler(httpd_req_t *req) {
    extern const uint8_t epilepsy_sans_eot_start[] asm("_binary_epilepsy_sans_eot_gz_start");
    extern const uint8_t epilepsy_sans_eot_end[] asm("_binary_epilepsy_sans_eot_gz_end");
    const size_t epilepsy_sans_eot_size = (epilepsy_sans_eot_end - epilepsy_sans_eot_start);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)epilepsy_sans_eot_start, epilepsy_sans_eot_size);
}
static esp_err_t epilepsy_sans_svg_get_handler(httpd_req_t *req) {
    extern const uint8_t epilepsy_sans_svg_start[] asm("_binary_epilepsy_sans_svg_gz_start");
    extern const uint8_t epilepsy_sans_svg_end[] asm("_binary_epilepsy_sans_svg_gz_end");
    const size_t epilepsy_sans_svg_size = (epilepsy_sans_svg_end - epilepsy_sans_svg_start);
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)epilepsy_sans_svg_start, epilepsy_sans_svg_size);
}

static esp_err_t epilepsy_sans_ttf_get_handler(httpd_req_t *req) {
    extern const uint8_t epilepsy_sans_ttf_start[] asm("_binary_epilepsy_sans_ttf_gz_start");
    extern const uint8_t epilepsy_sans_ttf_end[] asm("_binary_epilepsy_sans_ttf_gz_end");
    const size_t epilepsy_sans_ttf_size = (epilepsy_sans_ttf_end - epilepsy_sans_ttf_start);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)epilepsy_sans_ttf_start, epilepsy_sans_ttf_size);
}
static esp_err_t epilepsy_sans_woff_get_handler(httpd_req_t *req) {
    extern const uint8_t epilepsy_sans_woff_start[] asm("_binary_epilepsy_sans_woff_start");
    extern const uint8_t epilepsy_sans_woff_end[] asm("_binary_epilepsy_sans_woff_end");
    const size_t epilepsy_sans_woff_size = (epilepsy_sans_woff_end - epilepsy_sans_woff_start);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)epilepsy_sans_woff_start, epilepsy_sans_woff_size);
}
static esp_err_t epilepsy_sans_woff2_get_handler(httpd_req_t *req) {
    extern const uint8_t epilepsy_sans_woff2_start[] asm("_binary_epilepsy_sans_woff2_start");
    extern const uint8_t epilepsy_sans_woff2_end[] asm("_binary_epilepsy_sans_woff2_end");
    const size_t epilepsy_sans_woff2_size = (epilepsy_sans_woff2_end - epilepsy_sans_woff2_start);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)epilepsy_sans_woff2_start, epilepsy_sans_woff2_size);
}

static esp_err_t webfont_icons_svg_get_handler(httpd_req_t *req) {
    extern const uint8_t webfont_icons_svg_start[] asm("_binary_webfont_icons_svg_gz_start");
    extern const uint8_t webfont_icons_svg_end[] asm("_binary_webfont_icons_svg_gz_end");
    const size_t webfont_icons_svg_size = (webfont_icons_svg_end - webfont_icons_svg_start);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)webfont_icons_svg_start, webfont_icons_svg_size);
}

#define IS_FILE_EXT(filename, ext) (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename) {
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize) {
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        return NULL;
    }

    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

void setRespCORSHeaders(httpd_req_t *req, const char *methods) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "access-control-allow-origin,content-type");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", methods);
    httpd_resp_set_type(req, "text/plain");
}

static esp_err_t file_get_handler(httpd_req_t *req, const char *filename) {
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const size_t base_pathlen = strlen(REQ_CTX->base_path);
    size_t pathlen = strlen(filename);
    strcpy(filepath, REQ_CTX->base_path);
    strlcpy(filepath + base_pathlen, filename, pathlen + 1);

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    // set_content_type_from_file(req, filename);
    setRespCORSHeaders(req, "GET");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    char *chunk = REQ_CTX->scratch;
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fflush(fd);
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    fflush(fd);
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t download_get_handler(httpd_req_t *req) {
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, REQ_CTX->base_path, req->uri, sizeof(filepath));
    ESP_LOGI(TAG, "filename - %s", filename);
    ESP_LOGI(TAG, "filepath %s", filepath);
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (filename[strlen(filename) - 1] == '/') {
        if (webOpenedCounter < 255)
            webOpenedCounter++;
        return index_html_get_handler(req);
    }

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0 || strcmp(filename, "/buzzer") == 0) {
            if (webOpenedCounter < 255)
                webOpenedCounter++;
            return index_html_get_handler(req);
        } else if (strcmp(filename, "/favicon.ico") == 0) {
            return favicon_get_handler(req);
        } else if (strcmp(filename, "/main.css") == 0 || strcmp(filename, "/css/main.css") == 0) {
            return main_css_get_handler(req);
        } else if (strcmp(filename, "/main.js") == 0) {
            return main_js_get_handler(req);
        } else if (strcmp(filename, "/apple-touch-icon.png") == 0) {
            return apple_touch_icon_get_handler(req);
        } else if (strcmp(filename, "/1.js") == 0) {
            return ch_1_js_get_handler(req);
        } else if (strcmp(filename, "/epilepsy-sans.eot") == 0) {
            return epilepsy_sans_eot_get_handler(req);
        } else if (strcmp(filename, "/epilepsy-sans.svg") == 0) {
            return epilepsy_sans_svg_get_handler(req);
        } else if (strcmp(filename, "/epilepsy-sans.ttf") == 0) {
            return epilepsy_sans_ttf_get_handler(req);
        } else if (strcmp(filename, "/epilepsy-sans.woff") == 0) {
            return epilepsy_sans_woff_get_handler(req);
        } else if (strcmp(filename, "/epilepsy-sans.woff2") == 0) {
            return epilepsy_sans_woff2_get_handler(req);
        } else if (strcmp(filename, "/webfont-icons.svg") == 0) {
            return webfont_icons_svg_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        // httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");

        /* Captive portal redirect*/
        httpd_resp_set_status(req, "302 Temporary Redirect");
        httpd_resp_set_hdr(req, "Location", "/");
        // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
        httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Redirecting to root");

        return ESP_OK;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);
    char *chunk = REQ_CTX->scratch;
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fflush(fd);
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

    } while (chunksize != 0);

    fflush(fd);
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req) {
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, REQ_CTX->base_path, req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        ESP_LOGE(TAG, "File already exists : %s", filepath);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    if (req->content_len > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File size must be less than " MAX_FILE_SIZE_STR "!");
        return ESP_FAIL;
    }

    if (recv_body_to_file(req, filepath, false) != ESP_OK)
        return ESP_FAIL;

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t delete_post_handler(httpd_req_t *req) {
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, REQ_CTX->base_path, req->uri + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file : %s", filename);
    unlink(filepath);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

static esp_err_t system_info_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddStringToObject(root, "app_version", esp_app_get_description()->version);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t set_picture_post_handler(httpd_req_t *req) {
    updateLastActivity();
    if (imageSetCounter < 255)
        imageSetCounter++;

    char *buf = REQ_CTX->scratch;
    if (recv_json_body(req, buf) != ESP_OK)
        return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "Json not parsed!");
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGI(TAG, "Error at position %i \n", (int)(error_ptr - buf));
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Validate into a separate buffer so a bad request never touches the display.
    static Image picture; // 2.4 KB: too large for the httpd task stack
    const char *error = parse_picture(root, &picture);
    cJSON_Delete(root);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "frames count : %i", picture.framesCount);
    set_custom_image(&picture);

    setRespCORSHeaders(req, "POST");
    httpd_resp_sendstr(req, "Picture pixels setted successfully");
    return ESP_OK;
}

static esp_err_t set_picture_options_handler(httpd_req_t *req) {
    setRespCORSHeaders(req, "POST");
    httpd_resp_sendstr(req, "Picture pixels upload method");
    return ESP_OK;
}

static esp_err_t projects_post_handler(httpd_req_t *req) {
    updateLastActivity();
    if (projectSaveCounter < 255)
        projectSaveCounter++;

    char filepath[FILE_PATH_MAX];

    const char *filename = "/projects.json";

    const size_t base_pathlen = strlen(REQ_CTX->base_path);
    size_t pathlen = strlen(filename);
    strcpy(filepath, REQ_CTX->base_path);
    strlcpy(filepath + base_pathlen, filename, pathlen + 1);

    if (req->content_len > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File size must be less than " MAX_FILE_SIZE_STR "!");
        return ESP_FAIL;
    }

    if (recv_body_to_file(req, filepath, true) != ESP_OK)
        return ESP_FAIL;

    setRespCORSHeaders(req, "POST");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t projects_get_handler(httpd_req_t *req) { return file_get_handler(req, "/projects.json"); }

static esp_err_t projects_options_handler(httpd_req_t *req) {
    setRespCORSHeaders(req, "POST,GET");
    httpd_resp_sendstr(req, "Picture pixels upload method");
    return ESP_OK;
}

static esp_err_t buzzer_melody_post_handler(httpd_req_t *req) {
    char *buf = REQ_CTX->scratch;
    if (recv_json_body(req, buf) != ESP_OK)
        return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    const char *melody = cJSON_GetStringValue(cJSON_GetObjectItem(root, "melody"));
    if (melody == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected {\"melody\": \"<RTTTL>\"}");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Recieved melody %s", melody);
    int status = parse_rtttl(melody, strlen(melody));
    cJSON_Delete(root);

    if (status < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to play buzzer");
        return ESP_OK;
    }

    setRespCORSHeaders(req, "POST");
    httpd_resp_sendstr(req, "Melody setted successfully");

    return ESP_OK;
}

static esp_err_t melody_options_handler(httpd_req_t *req) {
    setRespCORSHeaders(req, "POST,GET");
    httpd_resp_sendstr(req, "Picture pixels upload method");
    return ESP_OK;
}

static esp_err_t buzzer_melodies_list_post_handler(httpd_req_t *req) {
    char filepath[FILE_PATH_MAX];

    const char *filename = "/melodies_list.json";

    const size_t base_pathlen = strlen(REQ_CTX->base_path);
    size_t pathlen = strlen(filename);
    strcpy(filepath, REQ_CTX->base_path);
    strlcpy(filepath + base_pathlen, filename, pathlen + 1);

    if (req->content_len > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File size must be less than " MAX_FILE_SIZE_STR "!");
        return ESP_FAIL;
    }

    if (recv_body_to_file(req, filepath, true) != ESP_OK)
        return ESP_FAIL;

    setRespCORSHeaders(req, "POST");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t buzzer_melodies_list_get_handler(httpd_req_t *req) { return file_get_handler(req, "/melodies_list.json"); }

static esp_err_t melodies_list_options_handler(httpd_req_t *req) {
    setRespCORSHeaders(req, "POST,GET");
    httpd_resp_sendstr(req, "Picture pixels upload method");
    return ESP_OK;
}

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

esp_err_t start_file_server(const char *base_path) {
    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path, sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.stack_size = 8196;
    // config.max_open_sockets = 5;
    // config.backlog_conn = 3;
    config.max_uri_handlers = 24;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(registerHomeWiFiSettings(server));
    ESP_ERROR_CHECK(registerOtaUpload(server));

    httpd_uri_t system_info_get_uri = {.uri = "/api/v1/system/info", .method = HTTP_GET, .handler = system_info_get_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &system_info_get_uri);

    httpd_uri_t set_picture_post_uri = {.uri = "/api/v1/led/picture", .method = HTTP_POST, .handler = set_picture_post_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_picture_post_uri);

    httpd_uri_t set_picture_options_uri = {.uri = "/api/v1/led/picture", .method = HTTP_OPTIONS, .handler = set_picture_options_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_picture_options_uri);

    httpd_uri_t projects_post_uri = {.uri = "/api/v1/projects", .method = HTTP_POST, .handler = projects_post_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &projects_post_uri);

    httpd_uri_t projects_get_uri = {.uri = "/api/v1/projects", .method = HTTP_GET, .handler = projects_get_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &projects_get_uri);

    httpd_uri_t projects_options_uri = {.uri = "/api/v1/projects", .method = HTTP_OPTIONS, .handler = projects_options_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &projects_options_uri);

    httpd_uri_t set_buzzer_melody_post_uri = {.uri = "/api/v1/buzzer/melody", .method = HTTP_POST, .handler = buzzer_melody_post_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_buzzer_melody_post_uri);

    httpd_uri_t set_buzzer_melody_options_uri = {.uri = "/api/v1/buzzer/melody", .method = HTTP_OPTIONS, .handler = melody_options_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_buzzer_melody_options_uri);

    httpd_uri_t set_melodies_list_get_uri = {.uri = "/api/v1/buzzer/melodyiesList", .method = HTTP_GET, .handler = buzzer_melodies_list_get_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_melodies_list_get_uri);

    httpd_uri_t set_melodies_list_post_uri = {.uri = "/api/v1/buzzer/melodyiesList", .method = HTTP_POST, .handler = buzzer_melodies_list_post_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_melodies_list_post_uri);

    httpd_uri_t set_melodies_list_options_uri = {.uri = "/api/v1/buzzer/melodyiesList", .method = HTTP_OPTIONS, .handler = melodies_list_options_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &set_melodies_list_options_uri);

    httpd_uri_t file_download = {.uri = "/*", .method = HTTP_GET, .handler = download_get_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &file_download);

    httpd_uri_t file_upload = {.uri = "/upload/*", .method = HTTP_POST, .handler = upload_post_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &file_upload);

    httpd_uri_t file_delete = {.uri = "/delete/*", .method = HTTP_POST, .handler = delete_post_handler, .user_ctx = server_data};
    httpd_register_uri_handler(server, &file_delete);

    // httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    return ESP_OK;
}

esp_err_t mount_storage(const char *base_path) {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0;
    size_t used = 0;
    ret = esp_littlefs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}
