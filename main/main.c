#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "hapanel_ui.h"

static const char *TAG = "hapanel";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HAPanel firmware");

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery before it can be used");
    } else if (nvs_result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_result));
    }

    const bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        ESP_LOGI(TAG, "PSRAM ready: %u bytes free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "PSRAM is not initialized");
    }

    const hapanel_ui_status_item_t status_items[] = {
        {.label = "Display", .value = "Ready", .level = HAPANEL_UI_STATUS_OK},
        {.label = "Touch", .value = "BSP online", .level = HAPANEL_UI_STATUS_OK},
        {.label = "Storage",
         .value = nvs_result == ESP_OK ? "Ready" : "Needs attention",
         .level = nvs_result == ESP_OK ? HAPANEL_UI_STATUS_OK : HAPANEL_UI_STATUS_WARNING},
        {.label = "Wi-Fi", .value = "Not configured", .level = HAPANEL_UI_STATUS_PENDING},
        {.label = "MQTT", .value = "Offline", .level = HAPANEL_UI_STATUS_OFFLINE},
        {.label = "OTA", .value = "Pending", .level = HAPANEL_UI_STATUS_OFFLINE},
    };

    const hapanel_ui_status_t ui_status = {
        .items = status_items,
        .item_count = sizeof(status_items) / sizeof(status_items[0]),
        .psram_ready = psram_ready,
    };

    bsp_display_start();

    bsp_display_lock(0);
    hapanel_ui_show_root(&ui_status);
    bsp_display_unlock();
}
