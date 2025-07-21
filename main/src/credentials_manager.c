#include "credentials_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CREDENTIALS";
static nvs_handle_t credentials_handle;
static bool credentials_initialized = false;

esp_err_t credentials_init(void) {
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &credentials_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    credentials_initialized = true;
    ESP_LOGI(TAG, "Credentials manager initialized");
    return ESP_OK;
}

esp_err_t credentials_load(credentials_t *creds) {
    if (!credentials_initialized) {
        ESP_LOGE(TAG, "Credentials manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (creds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(creds, 0, sizeof(credentials_t));
    
    size_t required_size;
    esp_err_t err;
    
    // Load WiFi SSID
    required_size = MAX_SSID_LEN;
    err = nvs_get_str(credentials_handle, NVS_WIFI_SSID_KEY, creds->wifi_ssid, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi SSID not found in NVS");
        return err;
    }
    
    // Load WiFi Password
    required_size = MAX_PASSWORD_LEN;
    err = nvs_get_str(credentials_handle, NVS_WIFI_PASS_KEY, creds->wifi_password, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi password not found in NVS");
        return err;
    }
    
    // Load Firebase Project ID
    required_size = MAX_PROJECT_ID_LEN;
    err = nvs_get_str(credentials_handle, NVS_FIREBASE_PROJECT_ID_KEY, creds->firebase_project_id, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Firebase project ID not found in NVS");
        return err;
    }
    
    // Load Firebase Database URL
    required_size = MAX_DB_URL_LEN;
    err = nvs_get_str(credentials_handle, NVS_FIREBASE_DB_URL_KEY, creds->firebase_db_url, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Firebase database URL not found in NVS");
        return err;
    }
    
    // Load Firebase API Key
    required_size = MAX_API_KEY_LEN;
    err = nvs_get_str(credentials_handle, NVS_FIREBASE_API_KEY_KEY, creds->firebase_api_key, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Firebase API key not found in NVS");
        return err;
    }
    
    ESP_LOGI(TAG, "Credentials loaded successfully");
    ESP_LOGI(TAG, "WiFi SSID: %s", creds->wifi_ssid);
    ESP_LOGI(TAG, "Firebase Project ID: %s", creds->firebase_project_id);
    
    return ESP_OK;
}

esp_err_t credentials_save(const credentials_t *creds) {
    if (!credentials_initialized) {
        ESP_LOGE(TAG, "Credentials manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (creds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err;
    
    // Save WiFi credentials
    err = nvs_set_str(credentials_handle, NVS_WIFI_SSID_KEY, creds->wifi_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(credentials_handle, NVS_WIFI_PASS_KEY, creds->wifi_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi password: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save Firebase credentials
    err = nvs_set_str(credentials_handle, NVS_FIREBASE_PROJECT_ID_KEY, creds->firebase_project_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Firebase project ID: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(credentials_handle, NVS_FIREBASE_DB_URL_KEY, creds->firebase_db_url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Firebase database URL: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(credentials_handle, NVS_FIREBASE_API_KEY_KEY, creds->firebase_api_key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Firebase API key: %s", esp_err_to_name(err));
        return err;
    }
    
    // Commit changes
    err = nvs_commit(credentials_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Credentials saved successfully");
    return ESP_OK;
}

esp_err_t credentials_set_wifi(const char *ssid, const char *password) {
    if (!credentials_initialized) {
        ESP_LOGE(TAG, "Credentials manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    credentials_t creds;
    esp_err_t err = credentials_load(&creds);
    if (err != ESP_OK) {
        // If credentials don't exist, initialize empty structure
        memset(&creds, 0, sizeof(credentials_t));
    }
    
    // Update WiFi credentials
    strncpy(creds.wifi_ssid, ssid, MAX_SSID_LEN - 1);
    strncpy(creds.wifi_password, password, MAX_PASSWORD_LEN - 1);
    
    return credentials_save(&creds);
}

esp_err_t credentials_set_firebase(const char *project_id, const char *db_url, const char *api_key) {
    if (!credentials_initialized) {
        ESP_LOGE(TAG, "Credentials manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (project_id == NULL || db_url == NULL || api_key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    credentials_t creds;
    esp_err_t err = credentials_load(&creds);
    if (err != ESP_OK) {
        // If credentials don't exist, initialize empty structure
        memset(&creds, 0, sizeof(credentials_t));
    }
    
    // Update Firebase credentials
    strncpy(creds.firebase_project_id, project_id, MAX_PROJECT_ID_LEN - 1);
    strncpy(creds.firebase_db_url, db_url, MAX_DB_URL_LEN - 1);
    strncpy(creds.firebase_api_key, api_key, MAX_API_KEY_LEN - 1);
    
    return credentials_save(&creds);
}

esp_err_t credentials_erase_all(void) {
    if (!credentials_initialized) {
        ESP_LOGE(TAG, "Credentials manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = nvs_erase_all(credentials_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_commit(credentials_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "All credentials erased");
    return ESP_OK;
}

bool credentials_exist(void) {
    if (!credentials_initialized) {
        return false;
    }
    
    size_t required_size;
    esp_err_t err = nvs_get_str(credentials_handle, NVS_WIFI_SSID_KEY, NULL, &required_size);
    
    return (err == ESP_OK);
}