#pragma once

#include "esp_err.h"
#include "hapanel_runtime.h"

esp_err_t hapanel_ota_init(hapanel_runtime_t *runtime);
esp_err_t hapanel_ota_mark_boot_valid(hapanel_runtime_t *runtime);
