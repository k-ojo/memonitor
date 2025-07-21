#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "config.h"
#include "credentials_manager.h"
#include "wifi_manager.h"
#include "firebase_manager.h"
#include "camera_manager.h"

static const char *TAG = "MAIN";
  
// Add this to your main.c before credentials_init()

void debug_nvs_partition(void) {
    ESP_LOGI("NVS_DEBUG", "=== NVS Partition Debug ===");
    
    // Check if NVS was initialized
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("NVS_DEBUG", "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_LOGI("NVS_DEBUG", "NVS flash init result: %s", esp_err_to_name(ret));
    
    // List all available entries across all namespaces
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    
    if (err != ESP_OK) {
        ESP_LOGW("NVS_DEBUG", "No NVS entries found or error occurred: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI("NVS_DEBUG", "Found NVS entries:");
    int entry_count = 0;
    
    do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        ESP_LOGI("NVS_DEBUG", "Entry %d - Namespace: %s, Key: %s, Type: %d", 
                 ++entry_count, info.namespace_name, info.key, info.type);
        
        // Move to next entry
        err = nvs_entry_next(&it);
        
    } while (err == ESP_OK);
    
    // Release the iterator
    nvs_release_iterator(it);
    ESP_LOGI("NVS_DEBUG", "Total entries found: %d", entry_count);
}
// Call this in app_main() before credentials_init():
// debug_nvs_partition();
// Function to generate timestamp
void generate_timestamp(char *buffer, size_t buffer_size) {
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buffer, buffer_size, "%Y%m%d_%H%M%S", &timeinfo);
}

// Function to setup default credentials (for first-time setup)
esp_err_t setup_default_credentials(void) {
    ESP_LOGI(TAG, "Setting up default credentials...");
    
    // Set WiFi credentials
    esp_err_t err = credentials_set_wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set Firebase credentials
    err = credentials_set_firebase(
        "your-project-id",
        "https://your-project-id-default-rtdb.firebaseio.com",
        "your-api-key"
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Firebase credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Default credentials set successfully");
    ESP_LOGW(TAG, "Please update the credentials with your actual values!");
    
    return ESP_OK;
}

// Main camera and upload task
void camera_upload_task(void *pvParameters) {
    char timestamp[64];
    
    while (1) {
        ESP_LOGI(TAG, "Taking picture...");
        
        // Generate timestamp
        generate_timestamp(timestamp, sizeof(timestamp));
        
        // Capture image as base64
        char *base64_image = NULL;
        size_t base64_len = 0;
        
        esp_err_t err = camera_capture_to_base64(&base64_image, &base64_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to capture image: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(NUMBER_OF_SECONDS * 1000));
            continue;
        }
        
        // Upload to Firebase
        ESP_LOGI(TAG, "Uploading image to Firebase...");
        err = firebase_upload_image(base64_image, timestamp);
        
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Image uploaded successfully!");
        } else {
            ESP_LOGE(TAG, "Failed to upload image to Firebase: %s", esp_err_to_name(err));
        }
        
        // Cleanup
        free(base64_image);
        
        ESP_LOGI(TAG, "Waiting %d seconds before next capture...", NUMBER_OF_SECONDS);
        vTaskDelay(pdMS_TO_TICKS(NUMBER_OF_SECONDS * 1000));
    }
}


void dump_credentials() {
    ESP_LOGI("CREDENTIALS_DUMP", "=== Dumping NVS Credentials ===");
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    
    if (err != ESP_OK) {
        ESP_LOGE("CREDENTIALS_DUMP", "Failed to open NVS namespace : %s", 
                esp_err_to_name(err));
        
        // Try without namespace (default)
        err = nvs_open(NULL, NVS_READONLY, &nvs);
        if (err != ESP_OK) {
            ESP_LOGE("CREDENTIALS_DUMP", "Failed to open default NVS: %s", esp_err_to_name(err));
            return;
        } else {
            ESP_LOGI("CREDENTIALS_DUMP", "Opened default namespace successfully");
        }
    } else {
        ESP_LOGI("CREDENTIALS_DUMP", "Opened namespace successfully");
    }
    
    // Test keys that might exist
    const char* test_keys[] = {
        "wifi_ssid", "wifi_pass", 
        "fb_project", "fb_project_id", 
        "fb_db_url", "fb_api_key"
    };
    
    char buffer[256];
    size_t len;
    
    for (int i = 0; i < 6; i++) {
        len = sizeof(buffer);
        memset(buffer, 0, sizeof(buffer));
        
        err = nvs_get_str(nvs, test_keys[i], buffer, &len);
        if (err == ESP_OK) {
            // Don't log passwords/keys fully for security
            if (strstr(test_keys[i], "pass") || strstr(test_keys[i], "key")) {
                ESP_LOGI("CREDENTIALS_DUMP", "Key '%s': %.*s*** (length: %d)", 
                         test_keys[i], 4, buffer, len-1);
            } else {
                ESP_LOGI("CREDENTIALS_DUMP", "Key '%s': %s (length: %d)", 
                         test_keys[i], buffer, len-1);
            }
        } else {
            ESP_LOGW("CREDENTIALS_DUMP", "Key '%s' not found: %s", 
                     test_keys[i], esp_err_to_name(err));
        }
    }
    
    nvs_close(nvs);
    ESP_LOGI("CREDENTIALS_DUMP", "=== End Credentials Dump ===");
}

void app_main(void) {
    
    ESP_LOGI(TAG, "Starting ESP32-CAM Application");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    debug_nvs_partition();
    dump_credentials();
    // Initialize credentials manager
    ESP_ERROR_CHECK(credentials_init());
    
    // Load credentials
    credentials_t creds;
    ret = credentials_load(&creds);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load credentials, setting up defaults");
        ESP_ERROR_CHECK(setup_default_credentials());
        
        // Try to load again
        ret = credentials_load(&creds);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to load credentials after setup: %s", esp_err_to_name(ret));
            return;
        }
    }
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Connecting to WiFi...");
    ret = wifi_init_sta(creds.wifi_ssid, creds.wifi_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize Firebase
    firebase_config_t firebase_config;
    strncpy(firebase_config.project_id, creds.firebase_project_id, sizeof(firebase_config.project_id) - 1);
    strncpy(firebase_config.database_url, creds.firebase_db_url, sizeof(firebase_config.database_url) - 1);
    strncpy(firebase_config.api_key, creds.firebase_api_key, sizeof(firebase_config.api_key) - 1);
    
    ESP_ERROR_CHECK(firebase_init(&firebase_config));
    
    // Initialize time (for better timestamps)
    setenv("TZ", "UTC", 1);
    tzset();
    
    // Initialize camera
    ESP_LOGI(TAG, "Initializing camera...");
    ESP_ERROR_CHECK(camera_init_default());
    
    // Print system information
    ESP_LOGI(TAG, "System initialized successfully");
    ESP_LOGI(TAG, "WiFi SSID: %s", creds.wifi_ssid);
    ESP_LOGI(TAG, "Firebase Project: %s", creds.firebase_project_id);
    
    char ip_str[16];
    if (wifi_get_ip_address(ip_str, sizeof(ip_str)) == ESP_OK) {
        ESP_LOGI(TAG, "IP Address: %s", ip_str);
    }
    
    // Start camera upload task
    xTaskCreate(camera_upload_task, "camera_upload", 8192, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Application started successfully");
}