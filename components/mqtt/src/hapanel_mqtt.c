#include "hapanel_mqtt.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "hapanel_mqtt";

static hapanel_runtime_t *mqtt_runtime;

static void set_mqtt_status(const char *value, hapanel_system_level_t level)
{
    hapanel_runtime_set_status(mqtt_runtime, HAPANEL_SYSTEM_MQTT, value, level);
}

static bool has_configured_broker(void)
{
    return strlen(CONFIG_HAPANEL_MQTT_BROKER_URI) > 0;
}

esp_err_t hapanel_mqtt_start(hapanel_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mqtt_runtime = runtime;

    if (!has_configured_broker()) {
        ESP_LOGI(TAG, "MQTT broker is not configured");
        set_mqtt_status("Not configured", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }

    ESP_LOGW(TAG,
             "MQTT broker is configured but the MQTT client dependency is not enabled yet: %s",
             CONFIG_HAPANEL_MQTT_BROKER_URI);
    set_mqtt_status("Client missing", HAPANEL_SYSTEM_LEVEL_WARNING);
    return ESP_ERR_NOT_SUPPORTED;
}
