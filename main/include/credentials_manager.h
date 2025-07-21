#ifndef CREDENTIALS_MANAGER_H
#define CREDENTIALS_MANAGER_H

#include "esp_err.h"
#include "config.h"
#include <stdbool.h>

// Credential structure
typedef struct {
    char wifi_ssid[MAX_SSID_LEN];
    char wifi_password[MAX_PASSWORD_LEN];
    char firebase_project_id[MAX_PROJECT_ID_LEN];
    char firebase_db_url[MAX_DB_URL_LEN];
    char firebase_api_key[MAX_API_KEY_LEN];
} credentials_t;

// Function declarations
esp_err_t credentials_init(void);
esp_err_t credentials_load(credentials_t *creds);
esp_err_t credentials_save(const credentials_t *creds);
esp_err_t credentials_set_wifi(const char *ssid, const char *password);
esp_err_t credentials_set_firebase(const char *project_id, const char *db_url, const char *api_key);
esp_err_t credentials_erase_all(void);
bool credentials_exist(void);

#endif // CREDENTIALS_MANAGER_H