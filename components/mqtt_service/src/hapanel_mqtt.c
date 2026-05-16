#include "hapanel_mqtt.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "hapanel_profile.h"
#include "mqtt_client.h"

static const char *TAG = "hapanel_mqtt";

static hapanel_runtime_t *mqtt_runtime;
static esp_mqtt_client_handle_t mqtt_client;
static bool event_handlers_registered;
static bool mqtt_started;

static void json_escape(const char *input, char *output, size_t output_size)
{
    if (output_size == 0) {
        return;
    }

    if (input == NULL) {
        input = "";
    }

    size_t out = 0;
    for (size_t in = 0; input[in] != '\0' && out + 1 < output_size; ++in) {
        const unsigned char ch = (unsigned char)input[in];
        const char *replacement = NULL;

        switch (ch) {
        case '"':
            replacement = "\\\"";
            break;
        case '\\':
            replacement = "\\\\";
            break;
        case '\b':
            replacement = "\\b";
            break;
        case '\f':
            replacement = "\\f";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            break;
        }

        if (replacement != NULL) {
            const size_t replacement_len = strlen(replacement);
            if (out + replacement_len >= output_size) {
                break;
            }
            memcpy(&output[out], replacement, replacement_len);
            out += replacement_len;
        } else if (ch < 0x20) {
            if (out + 6 >= output_size) {
                break;
            }
            snprintf(&output[out], output_size - out, "\\u%04x", ch);
            out += 6;
        } else {
            output[out++] = (char)ch;
        }
    }

    output[out] = '\0';
}

static void publish_device_status(esp_mqtt_client_handle_t client)
{
    const hapanel_profile_t *profile = hapanel_profile_active();
    const esp_app_desc_t *app = esp_app_get_description();

    char client_id[96];
    char app_version[48];
    char board_id[96];
    char board_name[96];
    char layout_id[48];
    char mcu[48];
    char display[96];
    char touch[48];
    char availability_topic[128];

    json_escape(CONFIG_HAPANEL_MQTT_CLIENT_ID, client_id, sizeof(client_id));
    json_escape(app->version, app_version, sizeof(app_version));
    json_escape(profile->board.id, board_id, sizeof(board_id));
    json_escape(profile->board.name, board_name, sizeof(board_name));
    json_escape(profile->layout.id, layout_id, sizeof(layout_id));
    json_escape(profile->board.mcu, mcu, sizeof(mcu));
    json_escape(profile->board.display, display, sizeof(display));
    json_escape(profile->board.touch, touch, sizeof(touch));
    json_escape(CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC,
                availability_topic,
                sizeof(availability_topic));

    char payload[768];
    const int payload_len = snprintf(
        payload,
        sizeof(payload),
        "{\"schema\":\"hapanel.device_status.v1\","
        "\"client_id\":\"%s\","
        "\"app_version\":\"%s\","
        "\"board\":{\"id\":\"%s\",\"name\":\"%s\",\"mcu\":\"%s\",\"flash_mb\":%u,"
        "\"psram_mb\":%u,\"display\":\"%s\",\"touch\":\"%s\"},"
        "\"layout\":{\"id\":\"%s\",\"width\":%u,\"height\":%u},"
        "\"mqtt\":{\"availability_topic\":\"%s\"}}",
        client_id,
        app_version,
        board_id,
        board_name,
        mcu,
        profile->board.flash_mb,
        profile->board.psram_mb,
        display,
        touch,
        layout_id,
        profile->layout.width,
        profile->layout.height,
        availability_topic);

    if (payload_len < 0 || payload_len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "MQTT device status payload truncated; skipping publish");
        return;
    }

    const int msg_id = esp_mqtt_client_publish(client,
                                               CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC,
                                               payload,
                                               payload_len,
                                               0,
                                               1);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish MQTT device status");
    } else {
        ESP_LOGI(TAG,
                 "Published MQTT device status: topic=%s msg_id=%d",
                 CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC,
                 msg_id);
    }
}

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

static esp_err_t start_mqtt_client(void)
{
    if (mqtt_client == NULL || mqtt_started) {
        return ESP_OK;
    }

    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        set_mqtt_status("Start error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return err;
    }

    mqtt_started = true;
    ESP_LOGI(TAG, "MQTT client connecting to broker: %s", CONFIG_HAPANEL_MQTT_BROKER_URI);
    set_mqtt_status("Connecting", HAPANEL_SYSTEM_LEVEL_PENDING);
    return ESP_OK;
}

static void network_event_handler(void *handler_args,
                                  esp_event_base_t event_base,
                                  int32_t event_id,
                                  void *event_data)
{
    (void)handler_args;
    (void)event_data;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Network is online; starting MQTT client");
        esp_err_t err = start_mqtt_client();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MQTT start after IP event failed: %s", esp_err_to_name(err));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (has_configured_broker()) {
            set_mqtt_status("Waiting Wi-Fi", HAPANEL_SYSTEM_LEVEL_PENDING);
        }
    }
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
        esp_mqtt_client_publish(event->client,
                                CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC,
                                "online",
                                0,
                                0,
                                1);
        publish_device_status(event->client);
        set_mqtt_status(CONFIG_HAPANEL_MQTT_BROKER_URI, HAPANEL_SYSTEM_LEVEL_OK);
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
        ESP_LOGI(TAG, "MQTT client is already initialized");
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
        .session.last_will.topic = CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC,
        .session.last_will.msg = "offline",
        .session.last_will.retain = true,
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

    if (!event_handlers_registered) {
        err = esp_event_handler_instance_register(IP_EVENT,
                                                  IP_EVENT_STA_GOT_IP,
                                                  network_event_handler,
                                                  NULL,
                                                  NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register MQTT IP event handler: %s", esp_err_to_name(err));
            set_mqtt_status("Event error", HAPANEL_SYSTEM_LEVEL_ERROR);
            esp_mqtt_client_destroy(mqtt_client);
            mqtt_client = NULL;
            return err;
        }

        err = esp_event_handler_instance_register(WIFI_EVENT,
                                                  WIFI_EVENT_STA_DISCONNECTED,
                                                  network_event_handler,
                                                  NULL,
                                                  NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register MQTT Wi-Fi event handler: %s", esp_err_to_name(err));
            set_mqtt_status("Event error", HAPANEL_SYSTEM_LEVEL_ERROR);
            esp_mqtt_client_destroy(mqtt_client);
            mqtt_client = NULL;
            return err;
        }

        event_handlers_registered = true;
    }

    ESP_LOGI(TAG, "MQTT client is waiting for Wi-Fi before connecting");
    set_mqtt_status("Waiting Wi-Fi", HAPANEL_SYSTEM_LEVEL_PENDING);
    return ESP_OK;
}
