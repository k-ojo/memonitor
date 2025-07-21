#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Function declarations
esp_err_t wifi_init_sta(const char *ssid, const char *password);
bool wifi_is_connected(void);
esp_err_t wifi_disconnect(void);
esp_err_t wifi_get_ip_address(char *ip_str, size_t max_len);

#endif // WIFI_MANAGER_H