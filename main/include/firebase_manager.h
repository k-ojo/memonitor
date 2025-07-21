#ifndef FIREBASE_MANAGER_H
#define FIREBASE_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// Firebase configuration structure
typedef struct {
    char project_id[64];
    char database_url[128];
    char api_key[128];
} firebase_config_t;

// Function declarations
esp_err_t firebase_init(const firebase_config_t *config);
esp_err_t firebase_upload_image(const char* base64_image, const char* timestamp);
esp_err_t firebase_upload_image_with_metadata(const char* base64_image, const char* timestamp, const char* metadata);
bool firebase_is_configured(void);

#endif // FIREBASE_MANAGER_H