#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "hapanel_runtime.h"

typedef struct {
    bool present;
    char label[17];
    uint8_t subtype;
    uint32_t address;
    uint32_t size;
} hapanel_ota_partition_info_t;

typedef struct {
    bool allowed;
    const char *reason;
    const char *running_label;
    const char *target_label;
    uint32_t target_address;
    uint32_t target_size;
} hapanel_ota_preflight_t;

typedef struct {
    hapanel_ota_partition_info_t running;
    hapanel_ota_partition_info_t boot;
    hapanel_ota_partition_info_t factory;
    hapanel_ota_partition_info_t ota_0;
    hapanel_ota_partition_info_t ota_1;
    hapanel_ota_partition_info_t target;
    bool boot_matches_running;
    bool reboot_required;
    bool rollback_enabled;
    const char *running_state;
    const char *phase;
} hapanel_ota_inventory_t;

typedef struct {
    bool active;
    esp_ota_handle_t handle;
    const esp_partition_t *target;
    hapanel_runtime_t *runtime;
    size_t expected_size;
    size_t written_size;
} hapanel_ota_session_t;

esp_err_t hapanel_ota_init(hapanel_runtime_t *runtime);
esp_err_t hapanel_ota_mark_boot_valid(hapanel_runtime_t *runtime);
esp_err_t hapanel_ota_preflight(hapanel_ota_preflight_t *preflight);
esp_err_t hapanel_ota_get_inventory(hapanel_ota_inventory_t *inventory);
esp_err_t hapanel_ota_begin(hapanel_runtime_t *runtime,
                            hapanel_ota_session_t *session,
                            size_t image_size);
esp_err_t hapanel_ota_write(hapanel_ota_session_t *session, const void *data, size_t size);
esp_err_t hapanel_ota_finish(hapanel_ota_session_t *session);
esp_err_t hapanel_ota_abort(hapanel_ota_session_t *session);
esp_err_t hapanel_ota_self_test_stage_running(hapanel_runtime_t *runtime);
esp_err_t hapanel_ota_self_test_stage_any_running(hapanel_runtime_t *runtime);
