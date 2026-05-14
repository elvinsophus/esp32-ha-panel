#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"

#include "hapanel_boot_ui.h"

static const char *TAG = "hapanel";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HAPanel firmware");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery before it can be used");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    }

    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM ready: %u bytes free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "PSRAM is not initialized");
    }

    bsp_display_start();

    bsp_display_lock(0);
    hapanel_boot_ui_show();
    bsp_display_unlock();
}
