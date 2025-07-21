#include "camera_manager.h"
#include "pin_config.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CAMERA_MGR";
static bool camera_initialized = false;

esp_err_t camera_init_with_config(const camera_config_params_t *params) {
    if (params == NULL) {
        ESP_LOGE(TAG, "Camera configuration parameters cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure flash LED pin as output and turn it off initially
    gpio_reset_pin(FLASH_GPIO_NUM);
    gpio_set_direction(FLASH_GPIO_NUM, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_GPIO_NUM, 0);  // off
    
    camera_config_t config = {
        .pin_pwdn  = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_sscb_sda = SIOD_GPIO_NUM,
        .pin_sscb_scl = SIOC_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = params->pixel_format,
        .frame_size = params->frame_size,
        .jpeg_quality = params->jpeg_quality,
        .fb_count = params->fb_count
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    camera_initialized = true;
    ESP_LOGI(TAG, "Camera initialized successfully");
    ESP_LOGI(TAG, "Frame size: %d, JPEG quality: %d, FB count: %d", 
             params->frame_size, params->jpeg_quality, params->fb_count);
    
    return ESP_OK;
}

esp_err_t camera_init_default(void) {
    camera_config_params_t default_params = {
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = CAMERA_FRAME_SIZE,
        .jpeg_quality = CAMERA_JPEG_QUALITY,
        .fb_count = CAMERA_FB_COUNT
    };
    
    return camera_init_with_config(&default_params);
}

esp_err_t camera_capture_to_base64(char **base64_output, size_t *output_len) {
    if (!camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (base64_output == NULL || output_len == NULL) {
        ESP_LOGE(TAG, "Output parameters cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Turn on the flash
    gpio_set_level(FLASH_GPIO_NUM, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to let flash stabilize

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        gpio_set_level(FLASH_GPIO_NUM, 0);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Image captured: %d bytes", fb->len);

    // Turn off the flash after capture
    gpio_set_level(FLASH_GPIO_NUM, 0);

    // Calculate Base64 encoding buffer size
    size_t encoded_len = 4 * ((fb->len + 2) / 3);
    unsigned char *encoded = malloc(encoded_len + 1);
    if (!encoded) {
        ESP_LOGE(TAG, "Failed to allocate memory for Base64 encoding");
        esp_camera_fb_return(fb);
        return ESP_ERR_NO_MEM;
    }

    size_t actual_len = 0;
    int base64_result = mbedtls_base64_encode(encoded, encoded_len, &actual_len, fb->buf, fb->len);
    if (base64_result != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed");
        free(encoded);
        esp_camera_fb_return(fb);
        return ESP_FAIL;
    }
    
    encoded[actual_len] = '\0';
    ESP_LOGI(TAG, "Base64 encoded image size: %d characters", actual_len);

    // Return the frame buffer
    esp_camera_fb_return(fb);
    
    // Set output parameters
    *base64_output = (char*)encoded;
    *output_len = actual_len;
    
    return ESP_OK;
}

esp_err_t camera_capture_raw(camera_fb_t **fb) {
    if (!camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (fb == NULL) {
        ESP_LOGE(TAG, "Frame buffer pointer cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Turn on the flash
    gpio_set_level(FLASH_GPIO_NUM, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to let flash stabilize

    *fb = esp_camera_fb_get();
    if (!*fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        gpio_set_level(FLASH_GPIO_NUM, 0);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Raw image captured: %d bytes", (*fb)->len);

    // Turn off the flash after capture
    gpio_set_level(FLASH_GPIO_NUM, 0);
    
    return ESP_OK;
}

void camera_return_frame_buffer(camera_fb_t *fb) {
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
}

esp_err_t camera_set_flash(bool enable) {
    if (!camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    gpio_set_level(FLASH_GPIO_NUM, enable ? 1 : 0);
    ESP_LOGI(TAG, "Flash %s", enable ? "ON" : "OFF");
    
    return ESP_OK;
}

bool camera_is_initialized(void) {
    return camera_initialized;
}

esp_err_t camera_deinit(void) {
    if (!camera_initialized) {
        ESP_LOGW(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera deinit failed: %s", esp_err_to_name(err));
        return err;
    }
    
    camera_initialized = false;
    ESP_LOGI(TAG, "Camera deinitialized");
    
    return ESP_OK;
}