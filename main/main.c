#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "hapanel_runtime.h"

static const char *TAG = "hapanel";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HAPanel firmware");

    hapanel_runtime_t runtime;
    hapanel_runtime_init(&runtime);

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery before it can be used");
    } else if (nvs_result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_result));
    }

    hapanel_runtime_set_status(&runtime,
                               HAPANEL_SYSTEM_STORAGE,
                               nvs_result == ESP_OK ? "Ready" : "Needs attention",
                               nvs_result == ESP_OK ? HAPANEL_SYSTEM_LEVEL_OK
                                                    : HAPANEL_SYSTEM_LEVEL_WARNING);

    const bool psram_ready = esp_psram_is_initialized();
    hapanel_runtime_set_psram_ready(&runtime, psram_ready);
    if (psram_ready) {
        ESP_LOGI(TAG, "PSRAM ready: %u bytes free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "PSRAM is not initialized");
    }

    bsp_display_start();
    hapanel_runtime_set_status(&runtime, HAPANEL_SYSTEM_DISPLAY, "Ready",
                               HAPANEL_SYSTEM_LEVEL_OK);
    hapanel_runtime_set_status(&runtime, HAPANEL_SYSTEM_TOUCH, "BSP online",
                               HAPANEL_SYSTEM_LEVEL_OK);

    bsp_display_lock(0);
    hapanel_runtime_render_root(&runtime);

    hapanel_runtime_set_status(&runtime, HAPANEL_SYSTEM_OTA, "Idle", HAPANEL_SYSTEM_LEVEL_OK);
    hapanel_runtime_refresh_root(&runtime);
    bsp_display_unlock();
}
