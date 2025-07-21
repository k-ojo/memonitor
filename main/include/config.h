#ifndef CONFIG_H
#define CONFIG_H

// Application configuration
#define NUMBER_OF_SECONDS 30

// Camera configuration
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 1

// WiFi configuration
#define WIFI_MAXIMUM_RETRY 10

// HTTP configuration
#define HTTP_RESPONSE_BUFFER_SIZE 1024
#define HTTP_TIMEOUT_MS 10000

// NVS storage keys for credentials - MUST match Python script keys
#define NVS_NAMESPACE "credentials"
#define NVS_WIFI_SSID_KEY "wifi_ssid"
#define NVS_WIFI_PASS_KEY "wifi_pass"
#define NVS_FIREBASE_PROJECT_ID_KEY "fb_project"    // Changed from "fb_project_id"
#define NVS_FIREBASE_DB_URL_KEY "fb_db_url"
#define NVS_FIREBASE_API_KEY_KEY "fb_api_key"

// Maximum credential lengths
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_PROJECT_ID_LEN 64
#define MAX_DB_URL_LEN 128
#define MAX_API_KEY_LEN 128

// WiFi connection event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#endif // CONFIG_H