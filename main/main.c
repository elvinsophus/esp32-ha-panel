#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "hapanel_system_status.h"
#include "hapanel_ui.h"

static const char *TAG = "hapanel";

static hapanel_ui_status_level_t ui_level_from_system(hapanel_system_level_t level)
{
    switch (level) {
    case HAPANEL_SYSTEM_LEVEL_OK:
        return HAPANEL_UI_STATUS_OK;
    case HAPANEL_SYSTEM_LEVEL_PENDING:
        return HAPANEL_UI_STATUS_PENDING;
    case HAPANEL_SYSTEM_LEVEL_WARNING:
        return HAPANEL_UI_STATUS_WARNING;
    case HAPANEL_SYSTEM_LEVEL_ERROR:
        return HAPANEL_UI_STATUS_ERROR;
    case HAPANEL_SYSTEM_LEVEL_OFFLINE:
    default:
        return HAPANEL_UI_STATUS_OFFLINE;
    }
}

static void build_ui_status(const hapanel_system_status_t *system_status,
                            hapanel_ui_status_item_t *ui_items,
                            hapanel_ui_status_t *ui_status)
{
    for (size_t i = 0; i < system_status->item_count; ++i) {
        ui_items[i].label = system_status->items[i].label;
        ui_items[i].value = system_status->items[i].value;
        ui_items[i].level = ui_level_from_system(system_status->items[i].level);
    }

    ui_status->items = ui_items;
    ui_status->item_count = system_status->item_count;
    ui_status->psram_ready = system_status->psram_ready;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HAPanel firmware");

    hapanel_system_status_t system_status;
    hapanel_system_status_init(&system_status);

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery before it can be used");
    } else if (nvs_result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_result));
    }

    hapanel_system_status_set(&system_status,
                              HAPANEL_SYSTEM_STORAGE,
                              nvs_result == ESP_OK ? "Ready" : "Needs attention",
                              nvs_result == ESP_OK ? HAPANEL_SYSTEM_LEVEL_OK
                                                   : HAPANEL_SYSTEM_LEVEL_WARNING);

    const bool psram_ready = esp_psram_is_initialized();
    hapanel_system_status_set_psram_ready(&system_status, psram_ready);
    if (psram_ready) {
        ESP_LOGI(TAG, "PSRAM ready: %u bytes free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "PSRAM is not initialized");
    }

    bsp_display_start();
    hapanel_system_status_set(&system_status, HAPANEL_SYSTEM_DISPLAY, "Ready",
                              HAPANEL_SYSTEM_LEVEL_OK);
    hapanel_system_status_set(&system_status, HAPANEL_SYSTEM_TOUCH, "BSP online",
                              HAPANEL_SYSTEM_LEVEL_OK);

    hapanel_ui_status_item_t ui_items[HAPANEL_SYSTEM_STATUS_COUNT];
    hapanel_ui_status_t ui_status;
    build_ui_status(&system_status, ui_items, &ui_status);

    bsp_display_lock(0);
    hapanel_ui_show_root(&ui_status);
    bsp_display_unlock();
}
