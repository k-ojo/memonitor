#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include "esp_err.h"
#include "esp_camera.h"
#include <stdbool.h>

// Camera configuration structure
typedef struct {
    pixformat_t pixel_format;
    framesize_t frame_size;
    int jpeg_quality;
    int fb_count;
} camera_config_params_t;

// Function declarations
esp_err_t camera_init_with_config(const camera_config_params_t *params);
esp_err_t camera_init_default(void);
esp_err_t camera_capture_to_base64(char **base64_output, size_t *output_len);
esp_err_t camera_capture_raw(camera_fb_t **fb);
void camera_return_frame_buffer(camera_fb_t *fb);
esp_err_t camera_set_flash(bool enable);
bool camera_is_initialized(void);
esp_err_t camera_deinit(void);

#endif // CAMERA_MANAGER_H