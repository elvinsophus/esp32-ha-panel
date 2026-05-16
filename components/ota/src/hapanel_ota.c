#include "hapanel_ota.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "sdkconfig.h"

static const char *TAG = "hapanel_ota";

static void log_partition(const char *role, const esp_partition_t *partition)
{
    if (partition == NULL) {
        ESP_LOGW(TAG, "%s partition: unavailable", role);
        return;
    }

    ESP_LOGI(TAG,
             "%s partition: label=%s subtype=0x%02x address=0x%06" PRIx32 " size=0x%06" PRIx32,
             role,
             partition->label,
             partition->subtype,
             partition->address,
             partition->size);
}

static void set_ota_status(hapanel_runtime_t *runtime,
                           const char *value,
                           hapanel_system_level_t level)
{
    if (runtime == NULL) {
        return;
    }

    hapanel_runtime_set_status(runtime, HAPANEL_SYSTEM_OTA, value, level);
}

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending_verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default:
        return "undefined";
    }
}

static esp_err_t get_running_ota_state(const esp_partition_t *running,
                                       esp_ota_img_states_t *state)
{
    if (running == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_ota_get_state_partition(running, state);
}

static void set_preflight_result(hapanel_ota_preflight_t *preflight,
                                 bool allowed,
                                 const char *reason,
                                 const esp_partition_t *running,
                                 const esp_partition_t *target)
{
    preflight->allowed = allowed;
    preflight->reason = reason;
    preflight->running_label = running != NULL ? running->label : NULL;
    preflight->target_label = target != NULL ? target->label : NULL;
    preflight->target_address = target != NULL ? target->address : 0;
    preflight->target_size = target != NULL ? target->size : 0;
}

static void log_preflight(const hapanel_ota_preflight_t *preflight)
{
    ESP_LOGI(TAG,
             "OTA preflight: allowed=%s reason=%s running=%s target=%s address=0x%06" PRIx32
             " size=0x%06" PRIx32,
             preflight->allowed ? "yes" : "no",
             preflight->reason,
             preflight->running_label != NULL ? preflight->running_label : "none",
             preflight->target_label != NULL ? preflight->target_label : "none",
             preflight->target_address,
             preflight->target_size);
}

esp_err_t hapanel_ota_preflight(hapanel_ota_preflight_t *preflight)
{
    if (preflight == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *preflight = (hapanel_ota_preflight_t){0};

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        set_preflight_result(preflight, false, "no running partition", NULL, NULL);
        log_preflight(preflight);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        set_preflight_result(preflight, false, "no update slot", running, NULL);
        log_preflight(preflight);
        return ESP_ERR_NOT_FOUND;
    }

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_result = get_running_ota_state(running, &running_state);
    if (state_result == ESP_OK && running_state == ESP_OTA_IMG_PENDING_VERIFY) {
        set_preflight_result(preflight, false, "running image pending verification", running, target);
        log_preflight(preflight);
        return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
    }

    if (state_result != ESP_OK && state_result != ESP_ERR_NOT_SUPPORTED &&
        state_result != ESP_ERR_NOT_FOUND) {
        set_preflight_result(preflight, false, "running state unavailable", running, target);
        log_preflight(preflight);
        return state_result;
    }
#endif

    set_preflight_result(preflight, true, "ready", running, target);
    log_preflight(preflight);
    return ESP_OK;
}

esp_err_t hapanel_ota_init(hapanel_runtime_t *runtime)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *ota_data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_DATA_OTA,
                                                               NULL);
    const esp_partition_t *boot = ota_data != NULL ? esp_ota_get_boot_partition() : running;
    const esp_partition_t *ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                            NULL);
    const esp_partition_t *ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                            NULL);

    ESP_LOGI(TAG, "Initializing OTA foundation");
    log_partition("running", running);
    log_partition("otadata", ota_data);
    log_partition("boot", boot);
    log_partition("ota_0", ota_0);
    log_partition("ota_1", ota_1);

    if (running == NULL || boot == NULL) {
        set_ota_status(runtime, "Partition error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NOT_FOUND;
    }

    if (running != boot) {
        ESP_LOGW(TAG, "Running partition differs from configured boot partition");
        set_ota_status(runtime, "Boot mismatch", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }

    const bool ota_slots_available = ota_0 != NULL && ota_1 != NULL;
    if (!ota_slots_available) {
        ESP_LOGW(TAG, "OTA update slots are not configured; firmware is factory-only for now");
        set_ota_status(runtime, "Factory only", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }

    hapanel_ota_preflight_t preflight;
    esp_err_t preflight_result = hapanel_ota_preflight(&preflight);
    if (preflight_result == ESP_OK) {
        ESP_LOGI(TAG, "Next OTA update target: %s", preflight.target_label);
    } else {
        ESP_LOGW(TAG, "OTA preflight is not ready: %s", preflight.reason);
    }

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_result = get_running_ota_state(running, &running_state);
    if (state_result == ESP_OK) {
        ESP_LOGI(TAG, "running OTA image state: %s", ota_state_name(running_state));
        if (running_state == ESP_OTA_IMG_PENDING_VERIFY) {
            set_ota_status(runtime, "Pending verify", HAPANEL_SYSTEM_LEVEL_PENDING);
            return ESP_OK;
        }
    } else if (state_result != ESP_ERR_NOT_SUPPORTED && state_result != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read running OTA image state: %s", esp_err_to_name(state_result));
        set_ota_status(runtime, "State unknown", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }
#else
    ESP_LOGW(TAG, "OTA rollback validation is disabled in sdkconfig");
    set_ota_status(runtime, "Rollback off", HAPANEL_SYSTEM_LEVEL_WARNING);
    return ESP_OK;
#endif

    set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
    return ESP_OK;
}

esp_err_t hapanel_ota_mark_boot_valid(hapanel_runtime_t *runtime)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        set_ota_status(runtime, "Partition error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NOT_FOUND;
    }

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ESP_LOGW(TAG, "Skipping boot validation mark; rollback support is disabled");
    set_ota_status(runtime, "Rollback off", HAPANEL_SYSTEM_LEVEL_WARNING);
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_result = get_running_ota_state(running, &running_state);
    if (state_result == ESP_ERR_NOT_SUPPORTED || state_result == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Running partition has no OTA state to validate");
        set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
        return ESP_OK;
    }

    if (state_result != ESP_OK) {
        ESP_LOGW(TAG, "Unable to read running OTA state for validation: %s",
                 esp_err_to_name(state_result));
        set_ota_status(runtime, "State unknown", HAPANEL_SYSTEM_LEVEL_WARNING);
        return state_result;
    }

    ESP_LOGI(TAG, "Boot validation check: running state=%s", ota_state_name(running_state));

    switch (running_state) {
    case ESP_OTA_IMG_PENDING_VERIFY: {
        esp_err_t mark_result = esp_ota_mark_app_valid_cancel_rollback();
        if (mark_result == ESP_OK) {
            ESP_LOGI(TAG, "Marked running OTA image valid");
            set_ota_status(runtime, "Validated", HAPANEL_SYSTEM_LEVEL_OK);
        } else {
            ESP_LOGE(TAG, "Failed to mark running OTA image valid: %s",
                     esp_err_to_name(mark_result));
            set_ota_status(runtime, "Validate failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        }
        return mark_result;
    }
    case ESP_OTA_IMG_NEW:
        set_ota_status(runtime, "Awaiting verify", HAPANEL_SYSTEM_LEVEL_PENDING);
        return ESP_OK;
    case ESP_OTA_IMG_INVALID:
    case ESP_OTA_IMG_ABORTED:
        set_ota_status(runtime, "Invalid image", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_FAIL;
    case ESP_OTA_IMG_VALID:
    case ESP_OTA_IMG_UNDEFINED:
    default:
        set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
        return ESP_OK;
    }
#endif
}
