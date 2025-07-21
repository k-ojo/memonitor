#include "camera_manager.h"
#include "pin_config.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CAMERA_MGR";
static bool camera_initialized = false;
static SemaphoreHandle_t camera_semaphore = NULL;
static TickType_t last_capture_time = 0;

// Enhanced retry configuration
#define MAX_CAPTURE_RETRIES 3
#define RETRY_DELAY_MS 100
#define MIN_CAPTURE_INTERVAL_MS 500  // Prevent rapid successive captures
#define FLASH_WARMUP_MS 200
#define FLASH_STABILIZE_MS 100

// Memory allocation strategy
#define PSRAM_MIN_SIZE_THRESHOLD 8192
#define BASE64_OVERHEAD_FACTOR 1.4f  // More accurate than 4/3

esp_err_t camera_init_with_config(const camera_config_params_t *params) {
    if (params == NULL) {
        ESP_LOGE(TAG, "Camera configuration parameters cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Prevent double initialization
    if (camera_initialized) {
        ESP_LOGW(TAG, "Camera already initialized");
        return ESP_OK;
    }
    
    // Create semaphore for thread safety
    if (camera_semaphore == NULL) {
        camera_semaphore = xSemaphoreCreateBinary();
        if (camera_semaphore == NULL) {
            ESP_LOGE(TAG, "Failed to create camera semaphore");
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(camera_semaphore);
    }
    
    // Configure flash LED pin
    gpio_reset_pin(FLASH_GPIO_NUM);
    gpio_set_direction(FLASH_GPIO_NUM, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_GPIO_NUM, 0);  // Start with flash off
    
    // Check PSRAM availability early
    bool psram_available = esp_psram_is_initialized();
    ESP_LOGI(TAG, "PSRAM %s", psram_available ? "available" : "not available");
    
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
        
        .xclk_freq_hz = 20000000,  // 20MHz - stable frequency
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = params->pixel_format,
        .frame_size = params->frame_size,
        .jpeg_quality = params->jpeg_quality,
        .fb_count = 1,
        
        // Optimize memory location based on PSRAM availability
        .fb_location = psram_available ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY
    };

    // Warn about memory constraints with large frames in DRAM
    if (!psram_available && params->frame_size > FRAMESIZE_VGA) {
        ESP_LOGW(TAG, "Large frame size (%d) without PSRAM - consider reducing size", 
                 params->frame_size);
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x (%s)", err, esp_err_to_name(err));
        goto cleanup;
    }

    // UNIFIED sensor configuration - balanced approach
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        // Set frame size first
        s->set_framesize(s, params->frame_size);
        
        // Balanced automatic controls for stability and quality
        s->set_gain_ctrl(s, 1);     // Enable automatic gain with constraints
        s->set_exposure_ctrl(s, 1); // Enable automatic exposure
        s->set_aec2(s, 0);          // Disable problematic AEC sensor
        s->set_ae_level(s, 0);      // Neutral auto-exposure level
        
        // Conservative manual overrides for stability
        if (s->set_agc_gain) s->set_agc_gain(s, 6);  // Moderate gain
        if (s->set_aec_value) s->set_aec_value(s, 400);  // Moderate exposure
        
        // Image quality - neutral settings
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        
        // White balance - automatic but constrained
        s->set_whitebal(s, 1);      // Enable AWB
        s->set_awb_gain(s, 1);      // Enable AWB gain
        s->set_wb_mode(s, 0);       // Auto WB mode
        
        // Quality enhancement features
        s->set_dcw(s, 1);           // Enable downsize cropping
        s->set_bpc(s, 0);           // Disable problematic black pixel correction
        s->set_wpc(s, 1);           // Enable white pixel correction
        
        // Enable lens correction if available
        if (s->set_lenc) s->set_lenc(s, 1);
        
        // Stability settings
        s->set_special_effect(s, 0); // No special effects
        s->set_hmirror(s, 0);        // No horizontal mirror
        s->set_vflip(s, 0);          // No vertical flip
        
        ESP_LOGI(TAG, "Sensor configured (PID: 0x%02x)", s->id.PID);
    } else {
        ESP_LOGW(TAG, "Failed to get sensor handle");
    }

    camera_initialized = true;
    last_capture_time = 0;  // Reset capture timing
    
    ESP_LOGI(TAG, "Camera initialized successfully");
    ESP_LOGI(TAG, "Config: Frame=%d, Quality=%d, PSRAM=%s", 
             params->frame_size, params->jpeg_quality, psram_available ? "YES" : "NO");
    ESP_LOGI(TAG, "Memory: Heap=%d, PSRAM=%d", 
             esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    return ESP_OK;

cleanup:
    if (camera_semaphore != NULL) {
        vSemaphoreDelete(camera_semaphore);
        camera_semaphore = NULL;
    }
    return err;
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

// Enhanced buffer clearing with timeout protection
static int clear_camera_buffers(int max_attempts, int delay_ms) {
    camera_fb_t *temp_fb = NULL;
    int cleared = 0;
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(2000);  // 2 second timeout
    
    for (int i = 0; i < max_attempts; i++) {
        // Timeout protection
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            ESP_LOGW(TAG, "Buffer clearing timeout after %d attempts", i);
            break;
        }
        
        temp_fb = esp_camera_fb_get();
        if (temp_fb == NULL) {
            break;  // No more buffers
        }
        
        esp_camera_fb_return(temp_fb);
        cleared++;
        
        if (delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
    
    if (cleared > 0) {
        ESP_LOGD(TAG, "Cleared %d frame buffers in %d ms", 
                 cleared, pdTICKS_TO_MS(xTaskGetTickCount() - start_time));
    }
    
    return cleared;
}

// Smart memory allocation for base64 encoding
static unsigned char* allocate_base64_buffer(size_t required_size, bool *used_psram) {
    unsigned char *buffer = NULL;
    *used_psram = false;
    
    // Try PSRAM first for large allocations
    if (esp_psram_is_initialized() && required_size > PSRAM_MIN_SIZE_THRESHOLD) {
        buffer = heap_caps_malloc(required_size, MALLOC_CAP_SPIRAM);
        if (buffer) {
            *used_psram = true;
            ESP_LOGD(TAG, "Allocated %zu bytes in PSRAM", required_size);
            return buffer;
        }
        ESP_LOGW(TAG, "PSRAM allocation failed, trying DRAM");
    }
    
    // Fallback to regular heap
    buffer = heap_caps_malloc(required_size, MALLOC_CAP_8BIT);
    if (buffer) {
        ESP_LOGD(TAG, "Allocated %zu bytes in DRAM", required_size);
    }
    
    return buffer;
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
    
    // Rate limiting - prevent rapid successive captures
    TickType_t current_time = xTaskGetTickCount();
    if (last_capture_time != 0) {
        TickType_t time_since_last = current_time - last_capture_time;
        TickType_t min_interval = pdMS_TO_TICKS(MIN_CAPTURE_INTERVAL_MS);
        
        if (time_since_last < min_interval) {
            TickType_t wait_time = min_interval - time_since_last;
            ESP_LOGD(TAG, "Rate limiting: waiting %d ms", pdTICKS_TO_MS(wait_time));
            vTaskDelay(wait_time);
        }
    }
    
    // Acquire semaphore with reasonable timeout
    if (xSemaphoreTake(camera_semaphore, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire camera semaphore");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t result = ESP_OK;
    camera_fb_t *fb = NULL;
    unsigned char *encoded = NULL;
    bool used_psram = false;
    
    ESP_LOGI(TAG, "Starting capture - Free: Heap=%d, PSRAM=%d", 
             esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // Retry loop with exponential backoff
    for (int attempt = 1; attempt <= MAX_CAPTURE_RETRIES; attempt++) {
        // Pre-capture preparation
        clear_camera_buffers(5, 5);
        vTaskDelay(pdMS_TO_TICKS(attempt == 1 ? 300 : 100));
        
        // Flash control with proper timing
        gpio_set_level(FLASH_GPIO_NUM, 1);
        vTaskDelay(pdMS_TO_TICKS(FLASH_WARMUP_MS));
        
        // Final stabilization
        clear_camera_buffers(3, 10);
        vTaskDelay(pdMS_TO_TICKS(FLASH_STABILIZE_MS));

        // Capture attempt
        fb = esp_camera_fb_get();
        
        if (fb != NULL && fb->len > 0 && fb->buf != NULL) {
            ESP_LOGI(TAG, "Capture successful on attempt %d: %d bytes, format=%d", 
                     attempt, fb->len, fb->format);
            break;
        }
        
        // Clean up failed attempt
        if (fb != NULL) {
            esp_camera_fb_return(fb);
            fb = NULL;
        }
        gpio_set_level(FLASH_GPIO_NUM, 0);
        
        if (attempt < MAX_CAPTURE_RETRIES) {
            int delay = RETRY_DELAY_MS * (1 << (attempt - 1));  // Exponential backoff
            ESP_LOGW(TAG, "Capture attempt %d failed, retrying in %d ms...", attempt, delay);
            vTaskDelay(pdMS_TO_TICKS(delay));
        }
    }
    
    // Turn off flash
    gpio_set_level(FLASH_GPIO_NUM, 0);
    
    if (fb == NULL || fb->len == 0 || fb->buf == NULL) {
        ESP_LOGE(TAG, "All capture attempts failed");
        result = ESP_FAIL;
        goto cleanup;
    }

    // Calculate base64 buffer size more accurately
    size_t encoded_len = (size_t)(fb->len * BASE64_OVERHEAD_FACTOR) + 64;  // Extra safety margin
    
    // Smart memory allocation
    encoded = allocate_base64_buffer(encoded_len + 1, &used_psram);
    if (!encoded) {
        ESP_LOGE(TAG, "Memory allocation failed: %zu bytes needed", encoded_len + 1);
        ESP_LOGE(TAG, "Available - Heap: %d, PSRAM: %d", 
                 esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // Clear buffer and encode
    memset(encoded, 0, encoded_len + 1);

    size_t actual_len = 0;
    int base64_result = mbedtls_base64_encode(encoded, encoded_len, &actual_len, fb->buf, fb->len);
    
    if (base64_result != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed: error=%d, input=%d bytes, buffer=%zu bytes", 
                 base64_result, fb->len, encoded_len);
        result = ESP_FAIL;
        goto cleanup;
    }
    
    encoded[actual_len] = '\0';
    
    ESP_LOGI(TAG, "Encoding successful: %zu chars (%.1f%% expansion, %s)", 
             actual_len, (float)actual_len * 100.0f / (float)fb->len,
             used_psram ? "PSRAM" : "DRAM");

    // Success - transfer ownership
    *base64_output = (char*)encoded;
    *output_len = actual_len;
    encoded = NULL;  // Prevent cleanup
    last_capture_time = xTaskGetTickCount();

cleanup:
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
    if (encoded != NULL) {
        free(encoded);
    }
    
    xSemaphoreGive(camera_semaphore);
    return result;
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
    
    // Rate limiting
    TickType_t current_time = xTaskGetTickCount();
    if (last_capture_time != 0) {
        TickType_t time_since_last = current_time - last_capture_time;
        TickType_t min_interval = pdMS_TO_TICKS(MIN_CAPTURE_INTERVAL_MS);
        
        if (time_since_last < min_interval) {
            vTaskDelay(min_interval - time_since_last);
        }
    }
    
    // Acquire semaphore
    if (xSemaphoreTake(camera_semaphore, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire camera semaphore for raw capture");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "Raw capture start - Free heap: %d bytes", esp_get_free_heap_size());
    
    // Prepare for capture
    clear_camera_buffers(5, 10);
    
    // Flash control
    gpio_set_level(FLASH_GPIO_NUM, 1);
    vTaskDelay(pdMS_TO_TICKS(FLASH_WARMUP_MS));

    // Final preparation and capture
    vTaskDelay(pdMS_TO_TICKS(FLASH_STABILIZE_MS));
    *fb = esp_camera_fb_get();

    // Turn off flash
    gpio_set_level(FLASH_GPIO_NUM, 0);
    
    if (!*fb) {
        ESP_LOGE(TAG, "Raw capture failed");
        xSemaphoreGive(camera_semaphore);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Raw capture successful: %d bytes, format=%d", (*fb)->len, (*fb)->format);
    last_capture_time = xTaskGetTickCount();
    
    xSemaphoreGive(camera_semaphore);
    return ESP_OK;
}

void camera_return_frame_buffer(camera_fb_t *fb) {
    if (fb != NULL) {
        esp_camera_fb_return(fb);
        ESP_LOGD(TAG, "Frame buffer returned");
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
    
    // Acquire semaphore before cleanup
    if (camera_semaphore != NULL) {
        xSemaphoreTake(camera_semaphore, pdMS_TO_TICKS(5000));
    }
    
    // Turn off flash
    gpio_set_level(FLASH_GPIO_NUM, 0);
    
    // Clear pending buffers
    int cleared = clear_camera_buffers(10, 10);
    ESP_LOGI(TAG, "Cleared %d buffers during deinit", cleared);
    
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera deinit failed: %s", esp_err_to_name(err));
        // Continue cleanup even if deinit failed
    }
    
    camera_initialized = false;
    last_capture_time = 0;
    
    // Clean up semaphore
    if (camera_semaphore != NULL) {
        vSemaphoreDelete(camera_semaphore);
        camera_semaphore = NULL;
    }
    
    ESP_LOGI(TAG, "Camera deinitialized %s", err == ESP_OK ? "successfully" : "with errors");
    return err;
}

esp_err_t camera_get_status(camera_manager_status_t *status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!camera_initialized) {
        memset(status, 0, sizeof(camera_manager_status_t));
        status->is_initialized = false;
        return ESP_ERR_INVALID_STATE;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    
    status->is_initialized = camera_initialized;
    status->free_heap = esp_get_free_heap_size();
    status->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    status->sensor_id = (s != NULL) ? s->id.PID : 0;
    
    return ESP_OK;
}

esp_err_t camera_diagnostic(void) {
    ESP_LOGI(TAG, "=== Camera Diagnostic ===");
    ESP_LOGI(TAG, "Status: %s", camera_initialized ? "INITIALIZED" : "NOT INITIALIZED");
    ESP_LOGI(TAG, "Free memory - Heap: %d, PSRAM: %d", 
             esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "PSRAM available: %s", esp_psram_is_initialized() ? "YES" : "NO");
    
    if (camera_initialized) {
        sensor_t *s = esp_camera_sensor_get();
if (s != NULL) {
    ESP_LOGI(TAG, "Sensor ID: 0x%02x", s->id.PID);

    // Access sensor status directly
    ESP_LOGI(TAG, "Current frame size: %d", s->status.framesize);
    ESP_LOGI(TAG, "Current quality: %d", s->status.quality);
    ESP_LOGI(TAG, "Brightness: %d", s->status.brightness);
    ESP_LOGI(TAG, "Contrast: %d", s->status.contrast);
    ESP_LOGI(TAG, "Saturation: %d", s->status.saturation);
} else {
    ESP_LOGW(TAG, "Unable to get sensor handle");
}

        
        // Check if buffers are available
        camera_fb_t *test_fb = esp_camera_fb_get();
        if (test_fb) {
            ESP_LOGI(TAG, "Buffer test: SUCCESS (%d bytes)", test_fb->len);
            esp_camera_fb_return(test_fb);
        } else {
            ESP_LOGW(TAG, "Buffer test: FAILED - no buffer available");
        }
    }
    
    ESP_LOGI(TAG, "Last capture: %d ms ago", 
             last_capture_time ? pdTICKS_TO_MS(xTaskGetTickCount() - last_capture_time) : 0);
    ESP_LOGI(TAG, "========================");
    
    return ESP_OK;
}