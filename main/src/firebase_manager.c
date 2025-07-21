#include "firebase_manager.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "FIREBASE_MGR";
static firebase_config_t firebase_config;
static bool firebase_configured = false;

// HTTP response buffer
static char http_response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
static int http_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER");
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    case HTTP_EVENT_ON_HEADERS_COMPLETE:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADERS_COMPLETE");
        break;
    }
    return ESP_OK;
}

esp_err_t firebase_init(const firebase_config_t *config)
{
    if (config == NULL)
    {
        ESP_LOGE(TAG, "Firebase configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(config->project_id) == 0 || strlen(config->database_url) == 0 || strlen(config->api_key) == 0)
    {
        ESP_LOGE(TAG, "Firebase configuration fields cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    strncpy(firebase_config.project_id, config->project_id, sizeof(firebase_config.project_id) - 1);
    strncpy(firebase_config.database_url, config->database_url, sizeof(firebase_config.database_url) - 1);
    strncpy(firebase_config.api_key, config->api_key, sizeof(firebase_config.api_key) - 1);

    firebase_configured = true;
    ESP_LOGI(TAG, "Firebase initialized with project ID: %s", firebase_config.project_id);

    return ESP_OK;
}

esp_err_t firebase_upload_image(const char *base64_image, const char *timestamp)
{
    return firebase_upload_image_with_metadata(base64_image, timestamp, NULL);
}

esp_err_t firebase_upload_image_with_metadata(const char* base64_image, const char* timestamp, const char* metadata) {
    if (!firebase_configured) {
        ESP_LOGE(TAG, "Firebase not configured");
        return ESP_ERR_INVALID_STATE;
    }

    if (base64_image == NULL || timestamp == NULL) {
        ESP_LOGE(TAG, "Base64 image and timestamp cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Create JSON payload
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON *image_data = cJSON_CreateString(base64_image);
    cJSON *ts = cJSON_CreateString(timestamp);

    if (image_data == NULL || ts == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON strings");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddItemToObject(json, "image", image_data);
    cJSON_AddItemToObject(json, "timestamp", ts);

    // Add metadata if provided
    if (metadata != NULL && strlen(metadata) > 0) {
        cJSON *meta = cJSON_CreateString(metadata);
        if (meta != NULL) {
            cJSON_AddItemToObject(json, "metadata", meta);
        }
    }

    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    // Create URL for Firebase Realtime Database
    char url[384];
    int url_len = snprintf(url, sizeof(url), "%s/images/%s.json?auth=%s",
                           firebase_config.database_url,
                           timestamp,
                           firebase_config.api_key);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .event_handler = http_event_handler,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Image uploaded successfully, Status = %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "Failed to upload image: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_Delete(json);
    free(json_string);

    return err;
}
