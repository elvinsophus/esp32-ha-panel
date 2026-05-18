#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "hapanel_mqtt.h"
#include "hapanel_network.h"
#include "hapanel_ota.h"
#include "hapanel_profile.h"
#include "hapanel_runtime.h"

static const char *TAG = "hapanel";
static hapanel_runtime_t runtime;

static void refresh_root_ui(void *context)
{
    hapanel_runtime_t *runtime = (hapanel_runtime_t *)context;

    if (bsp_display_lock(0)) {
        hapanel_runtime_refresh_current_page(runtime);
        bsp_display_unlock();
    }
}

static void handle_ui_page_request(hapanel_ui_page_id_t page, void *context)
{
    hapanel_runtime_t *runtime = (hapanel_runtime_t *)context;
    hapanel_runtime_handle_ui_page_request(runtime, page);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HAPanel firmware");

    hapanel_runtime_init(&runtime);

    const hapanel_profile_t *profile = hapanel_profile_active();
    ESP_LOGI(TAG,
             "Active profile: board=%s layout=%s display=%ux%u performance=%s theme=%s",
             profile->board.id,
             profile->layout.id,
             profile->layout.width,
             profile->layout.height,
             profile->performance.id,
             profile->theme.id);
    ESP_LOGI(TAG,
             "Test hardware: mcu=%s flash=%uMB psram=%uMB display=%s touch=%s",
             profile->board.mcu,
             profile->board.flash_mb,
             profile->board.psram_mb,
             profile->board.display,
             profile->board.touch);
    hapanel_runtime_set_status(&runtime, HAPANEL_SYSTEM_PROFILE, profile->layout.id,
                               HAPANEL_SYSTEM_LEVEL_OK);

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
    hapanel_ui_set_page_request_callback(handle_ui_page_request, &runtime);
    hapanel_runtime_set_status(&runtime, HAPANEL_SYSTEM_DISPLAY, "Ready",
                               HAPANEL_SYSTEM_LEVEL_OK);
    hapanel_runtime_set_status(&runtime, HAPANEL_SYSTEM_TOUCH, "BSP online",
                               HAPANEL_SYSTEM_LEVEL_OK);
    esp_err_t ota_result = hapanel_ota_init(&runtime);
    if (ota_result != ESP_OK) {
        ESP_LOGW(TAG, "OTA foundation init returned: %s", esp_err_to_name(ota_result));
    }

    bsp_display_lock(0);
    hapanel_runtime_render_root(&runtime);

    esp_err_t ota_valid_result = hapanel_ota_mark_boot_valid(&runtime);
    if (ota_valid_result != ESP_OK && ota_valid_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "OTA boot validation returned: %s", esp_err_to_name(ota_valid_result));
    }

#if CONFIG_HAPANEL_OTA_SELF_TEST_STAGE_FACTORY_ON_BOOT
    esp_err_t ota_self_test_result = hapanel_ota_self_test_stage_running(&runtime);
    if (ota_self_test_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "OTA self-test staging returned: %s",
                 esp_err_to_name(ota_self_test_result));
    }
#endif

    hapanel_runtime_refresh_root(&runtime);
    bsp_display_unlock();

    hapanel_runtime_set_refresh_callback(&runtime, refresh_root_ui, &runtime);

    esp_err_t network_result = hapanel_network_start(&runtime);
    if (network_result != ESP_OK) {
        ESP_LOGW(TAG, "Network start returned: %s", esp_err_to_name(network_result));
    }

    esp_err_t mqtt_result = hapanel_mqtt_start(&runtime);
    if (mqtt_result != ESP_OK && mqtt_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "MQTT start returned: %s", esp_err_to_name(mqtt_result));
    }
}
