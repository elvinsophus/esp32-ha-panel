#include "hapanel_mqtt.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "hapanel_mqtt";

static hapanel_runtime_t *mqtt_runtime;
static esp_mqtt_client_handle_t mqtt_client;

static void set_mqtt_status(const char *value, hapanel_system_level_t level)
{
    hapanel_runtime_set_status(mqtt_runtime, HAPANEL_SYSTEM_MQTT, value, level);
}

static bool has_text(const char *value)
{
    return value != NULL && strlen(value) > 0;
}

static bool has_configured_broker(void)
{
    return has_text(CONFIG_HAPANEL_MQTT_BROKER_URI);
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)event_base;

    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        set_mqtt_status("Connected", HAPANEL_SYSTEM_LEVEL_OK);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        set_mqtt_status("Disconnected", HAPANEL_SYSTEM_LEVEL_OFFLINE);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        set_mqtt_status("Error", HAPANEL_SYSTEM_LEVEL_WARNING);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG,
                 "MQTT data received: topic_len=%d data_len=%d",
                 event->topic_len,
                 event->data_len);
        break;
    default:
        ESP_LOGD(TAG, "MQTT event id=%" PRIi32, event_id);
        break;
    }
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

    if (mqtt_client != NULL) {
        ESP_LOGI(TAG, "MQTT client is already started");
        return ESP_OK;
    }

    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = CONFIG_HAPANEL_MQTT_BROKER_URI,
        .credentials.client_id = CONFIG_HAPANEL_MQTT_CLIENT_ID,
        .credentials.username = has_text(CONFIG_HAPANEL_MQTT_USERNAME)
                                     ? CONFIG_HAPANEL_MQTT_USERNAME
                                     : NULL,
        .credentials.authentication.password = has_text(CONFIG_HAPANEL_MQTT_PASSWORD)
                                                   ? CONFIG_HAPANEL_MQTT_PASSWORD
                                                   : NULL,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        set_mqtt_status("Init error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(mqtt_client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler,
                                                   NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        set_mqtt_status("Event error", HAPANEL_SYSTEM_LEVEL_ERROR);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return err;
    }

    err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        set_mqtt_status("Start error", HAPANEL_SYSTEM_LEVEL_ERROR);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client connecting to broker: %s", CONFIG_HAPANEL_MQTT_BROKER_URI);
    set_mqtt_status("Connecting", HAPANEL_SYSTEM_LEVEL_PENDING);
    return ESP_OK;
}
