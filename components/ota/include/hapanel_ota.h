#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hapanel_runtime.h"

typedef struct {
    bool allowed;
    const char *reason;
    const char *running_label;
    const char *target_label;
    uint32_t target_address;
    uint32_t target_size;
} hapanel_ota_preflight_t;

esp_err_t hapanel_ota_init(hapanel_runtime_t *runtime);
esp_err_t hapanel_ota_mark_boot_valid(hapanel_runtime_t *runtime);
esp_err_t hapanel_ota_preflight(hapanel_ota_preflight_t *preflight);
