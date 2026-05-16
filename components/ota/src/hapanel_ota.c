#include "hapanel_ota.h"

#include <inttypes.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

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

    set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
    return ESP_OK;
}
