#include "hapanel_mqtt.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "hapanel_profile.h"
#include "mqtt_client.h"

static const char *TAG = "hapanel_mqtt";

static hapanel_runtime_t *mqtt_runtime;
static esp_mqtt_client_handle_t mqtt_client;
static bool event_handlers_registered;
static bool mqtt_started;
static bool mqtt_connected;
static uint32_t published_state_revision;

static bool topic_matches(const esp_mqtt_event_handle_t event, const char *topic)
{
    return event->topic_len == (int)strlen(topic) &&
           strncmp(event->topic, topic, event->topic_len) == 0;
}

static const char *skip_ascii_space(const char *value)
{
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        ++value;
    }

    return value;
}

static bool is_ascii_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool payload_equals(const char *payload, const char *expected)
{
    payload = skip_ascii_space(payload);
    const size_t expected_len = strlen(expected);
    if (strncmp(payload, expected, expected_len) != 0) {
        return false;
    }

    payload += expected_len;
    while (is_ascii_space(*payload)) {
        ++payload;
    }

    return *payload == '\0';
}

static bool extract_json_string_field(const char *payload,
                                      const char *field,
                                      char *value,
                                      size_t value_size)
{
    if (value_size == 0) {
        return false;
    }
    value[0] = '\0';

    char pattern[48];
    const int pattern_len = snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    if (pattern_len < 0 || pattern_len >= (int)sizeof(pattern)) {
        return false;
    }

    const char *cursor = strstr(payload, pattern);
    if (cursor == NULL) {
        return false;
    }

    cursor += strlen(pattern);
    cursor = skip_ascii_space(cursor);
    if (*cursor != ':') {
        return false;
    }

    cursor = skip_ascii_space(cursor + 1);
    if (*cursor != '"') {
        return false;
    }
    ++cursor;

    size_t out = 0;
    while (*cursor != '\0' && *cursor != '"') {
        if (*cursor == '\\') {
            return false;
        }
        if (out + 1 >= value_size) {
            return false;
        }
        value[out++] = *cursor++;
    }

    if (*cursor != '"') {
        return false;
    }

    value[out] = '\0';
    return out > 0;
}

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

static const char *system_level_name(hapanel_system_level_t level)
{
    switch (level) {
    case HAPANEL_SYSTEM_LEVEL_OK:
        return "ok";
    case HAPANEL_SYSTEM_LEVEL_PENDING:
        return "pending";
    case HAPANEL_SYSTEM_LEVEL_OFFLINE:
        return "offline";
    case HAPANEL_SYSTEM_LEVEL_WARNING:
        return "warning";
    case HAPANEL_SYSTEM_LEVEL_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static bool append_text(char *buffer, size_t buffer_size, size_t *offset, const char *format, ...)
{
    if (*offset >= buffer_size) {
        return false;
    }

    va_list args;
    va_start(args, format);
    const int written = vsnprintf(&buffer[*offset], buffer_size - *offset, format, args);
    va_end(args);

    if (written < 0 || written >= (int)(buffer_size - *offset)) {
        return false;
    }

    *offset += written;
    return true;
}

static bool append_status_item_object(char *buffer,
                                      size_t buffer_size,
                                      size_t *offset,
                                      const char *field,
                                      const hapanel_system_status_item_t *item)
{
    char value[128];
    json_escape(item->value, value, sizeof(value));

    return append_text(buffer,
                       buffer_size,
                       offset,
                       "\"%s\":{\"value\":\"%s\",\"level\":\"%s\"},",
                       field,
                       value,
                       system_level_name(item->level));
}

static void publish_device_state(esp_mqtt_client_handle_t client, bool force)
{
    if (mqtt_runtime == NULL || client == NULL || !mqtt_connected) {
        return;
    }

    const hapanel_system_status_t *status = &mqtt_runtime->system_status;
    if (!force && published_state_revision == status->revision) {
        return;
    }

    char payload[1536];
    size_t offset = 0;
    if (!append_text(payload,
                     sizeof(payload),
                     &offset,
                     "{\"schema\":\"hapanel.device_state.v1\","
                     "\"revision\":%" PRIu32 ","
                     "\"uptime_ms\":%" PRIu64 ","
                     "\"psram_ready\":%s,",
                     status->revision,
                     (uint64_t)(esp_timer_get_time() / 1000),
                     status->psram_ready ? "true" : "false")) {
        ESP_LOGW(TAG, "MQTT device state payload header truncated; skipping publish");
        return;
    }

    if (!append_status_item_object(payload,
                                   sizeof(payload),
                                   &offset,
                                   "wifi",
                                   &status->items[HAPANEL_SYSTEM_WIFI]) ||
        !append_status_item_object(payload,
                                   sizeof(payload),
                                   &offset,
                                   "mqtt",
                                   &status->items[HAPANEL_SYSTEM_MQTT]) ||
        !append_status_item_object(payload,
                                   sizeof(payload),
                                   &offset,
                                   "ota",
                                   &status->items[HAPANEL_SYSTEM_OTA])) {
        ESP_LOGW(TAG, "MQTT device state connectivity payload truncated; skipping publish");
        return;
    }

    if (!append_text(payload, sizeof(payload), &offset, "\"services\":[")) {
        ESP_LOGW(TAG, "MQTT device state services header truncated; skipping publish");
        return;
    }

    for (size_t i = 0; i < status->item_count; ++i) {
        char label[64];
        char value[128];
        json_escape(status->items[i].label, label, sizeof(label));
        json_escape(status->items[i].value, value, sizeof(value));

        if (!append_text(payload,
                         sizeof(payload),
                         &offset,
                         "%s{\"label\":\"%s\",\"value\":\"%s\",\"level\":\"%s\"}",
                         i == 0 ? "" : ",",
                         label,
                         value,
                         system_level_name(status->items[i].level))) {
            ESP_LOGW(TAG, "MQTT device state payload truncated; skipping publish");
            return;
        }
    }

    if (!append_text(payload, sizeof(payload), &offset, "]}")) {
        ESP_LOGW(TAG, "MQTT device state payload footer truncated; skipping publish");
        return;
    }

    const int msg_id = esp_mqtt_client_publish(client,
                                               CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC,
                                               payload,
                                               (int)offset,
                                               0,
                                               1);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish MQTT device state");
    } else {
        published_state_revision = status->revision;
        ESP_LOGI(TAG,
                 "Published MQTT device state: topic=%s revision=%" PRIu32 " msg_id=%d",
                 CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC,
                 status->revision,
                 msg_id);
    }
}

static void publish_home_assistant_discovery(esp_mqtt_client_handle_t client)
{
#if CONFIG_HAPANEL_MQTT_HA_DISCOVERY_ENABLE
    const hapanel_profile_t *profile = hapanel_profile_active();
    const esp_app_desc_t *app = esp_app_get_description();

    char app_version[48];
    char board_name[96];
    char client_id[96];
    char status_topic[128];
    char state_topic[128];
    char command_topic[128];
    char availability_topic[128];

    json_escape(app->version, app_version, sizeof(app_version));
    json_escape(profile->board.name, board_name, sizeof(board_name));
    json_escape(CONFIG_HAPANEL_MQTT_CLIENT_ID, client_id, sizeof(client_id));
    json_escape(CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC, status_topic, sizeof(status_topic));
    json_escape(CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC, state_topic, sizeof(state_topic));
    json_escape(CONFIG_HAPANEL_MQTT_COMMAND_TOPIC, command_topic, sizeof(command_topic));
    json_escape(CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC,
                availability_topic,
                sizeof(availability_topic));

    char device_json[256];
    const int device_json_len = snprintf(device_json,
                                         sizeof(device_json),
                                         "\"device\":{\"identifiers\":[\"%s\"],"
                                         "\"name\":\"HAPanel\",\"manufacturer\":\"HAPanel\","
                                         "\"model\":\"%s\",\"sw_version\":\"%s\"},"
                                         "\"origin\":{\"name\":\"HAPanel\","
                                         "\"sw_version\":\"%s\"}",
                                         client_id,
                                         board_name,
                                         app_version,
                                         app_version);
    if (device_json_len < 0 || device_json_len >= (int)sizeof(device_json)) {
        ESP_LOGW(TAG, "Home Assistant discovery device metadata truncated; skipping publish");
        return;
    }

    const struct {
        const char *component;
        const char *object_id;
        const char *payload_format;
        const char *topic;
        const char *attributes_topic;
    } entities[] = {
        {
            .component = "sensor",
            .object_id = "hapanel_app_version",
            .payload_format =
                "{\"name\":\"App Version\","
                "\"unique_id\":\"%s_app_version\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.app_version }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = status_topic,
            .attributes_topic = status_topic,
        },
        {
            .component = "sensor",
            .object_id = "hapanel_uptime",
            .payload_format =
                "{\"name\":\"Uptime\","
                "\"unique_id\":\"%s_uptime\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ (value_json.uptime_ms / 1000) | int }}\","
                "\"unit_of_measurement\":\"s\","
                "\"state_class\":\"measurement\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "binary_sensor",
            .object_id = "hapanel_psram_ready",
            .payload_format =
                "{\"name\":\"PSRAM Ready\","
                "\"unique_id\":\"%s_psram_ready\","
                "\"entity_category\":\"diagnostic\","
                "\"device_class\":\"connectivity\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ 'ON' if value_json.psram_ready else 'OFF' }}\","
                "\"payload_on\":\"ON\","
                "\"payload_off\":\"OFF\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "sensor",
            .object_id = "hapanel_wifi_status",
            .payload_format =
                "{\"name\":\"Wi-Fi Status\","
                "\"unique_id\":\"%s_wifi_status\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.wifi.value }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "sensor",
            .object_id = "hapanel_mqtt_status",
            .payload_format =
                "{\"name\":\"MQTT Status\","
                "\"unique_id\":\"%s_mqtt_status\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.mqtt.value }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "sensor",
            .object_id = "hapanel_ota_status",
            .payload_format =
                "{\"name\":\"OTA Status\","
                "\"unique_id\":\"%s_ota_status\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.ota.value }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_status_refresh",
            .payload_format =
                "{\"name\":\"Refresh Status\","
                "\"unique_id\":\"%s_status_refresh\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"status_refresh\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ui_refresh",
            .payload_format =
                "{\"name\":\"Refresh UI\","
                "\"unique_id\":\"%s_ui_refresh\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ui_refresh\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
    };

    for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        char topic[160];
        const int topic_len = snprintf(topic,
                                       sizeof(topic),
                                       "%s/%s/%s/config",
                                       CONFIG_HAPANEL_MQTT_HA_DISCOVERY_PREFIX,
                                       entities[i].component,
                                       entities[i].object_id);
        if (topic_len < 0 || topic_len >= (int)sizeof(topic)) {
            ESP_LOGW(TAG, "Home Assistant discovery topic truncated; skipping publish");
            continue;
        }

        char payload[896];
        const int payload_len = snprintf(payload,
                                         sizeof(payload),
                                         entities[i].payload_format,
                                         client_id,
                                         entities[i].topic,
                                         availability_topic,
                                         entities[i].attributes_topic,
                                         device_json);

        if (payload_len < 0 || payload_len >= (int)sizeof(payload)) {
            ESP_LOGW(TAG, "Home Assistant discovery payload truncated; skipping publish");
            continue;
        }

        const int msg_id = esp_mqtt_client_publish(client, topic, payload, payload_len, 0, 1);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "Failed to publish Home Assistant discovery: topic=%s", topic);
        } else {
            ESP_LOGI(TAG,
                     "Published Home Assistant discovery: topic=%s msg_id=%d",
                     topic,
                     msg_id);
        }
    }
#else
    (void)client;
#endif
}

static void publish_command_result(esp_mqtt_client_handle_t client,
                                   const char *command_id,
                                   const char *command,
                                   const char *status,
                                   const char *reason)
{
    if (client == NULL || !mqtt_connected) {
        return;
    }

    char command_id_json[64];
    char command_json[64];
    char status_json[32];
    char reason_json[96];

    json_escape(command_id, command_id_json, sizeof(command_id_json));
    json_escape(command, command_json, sizeof(command_json));
    json_escape(status, status_json, sizeof(status_json));
    json_escape(reason, reason_json, sizeof(reason_json));

    const uint32_t revision = mqtt_runtime != NULL ? mqtt_runtime->system_status.revision : 0;
    char payload[384];
    const int payload_len = snprintf(
        payload,
        sizeof(payload),
        "{\"schema\":\"hapanel.command_result.v1\","
        "\"id\":\"%s\","
        "\"command\":\"%s\","
        "\"status\":\"%s\","
        "\"reason\":\"%s\","
        "\"revision\":%" PRIu32 ","
        "\"uptime_ms\":%" PRIu64 "}",
        command_id_json,
        command_json,
        status_json,
        reason_json,
        revision,
        (uint64_t)(esp_timer_get_time() / 1000));

    if (payload_len < 0 || payload_len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "MQTT command result payload truncated; skipping publish");
        return;
    }

    const int msg_id = esp_mqtt_client_publish(client,
                                               CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC,
                                               payload,
                                               payload_len,
                                               0,
                                               0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish MQTT command result");
    } else {
        ESP_LOGI(TAG,
                 "Published MQTT command result: topic=%s command=%s status=%s msg_id=%d",
                 CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC,
                 command_json,
                 status_json,
                 msg_id);
    }
}

static void status_change_callback(void *context)
{
    (void)context;
    publish_device_state(mqtt_client, false);
}

static void subscribe_command_topic(esp_mqtt_client_handle_t client)
{
    const int msg_id = esp_mqtt_client_subscribe(client,
                                                 CONFIG_HAPANEL_MQTT_COMMAND_TOPIC,
                                                 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to subscribe to MQTT command topic");
    } else {
        ESP_LOGI(TAG,
                 "Subscribed to MQTT command topic: topic=%s msg_id=%d",
                 CONFIG_HAPANEL_MQTT_COMMAND_TOPIC,
                 msg_id);
    }
}

static void handle_command_payload(esp_mqtt_client_handle_t client,
                                   const char *payload,
                                   int payload_len)
{
    if (payload_len <= 0) {
        ESP_LOGW(TAG, "Ignoring empty MQTT command payload");
        publish_command_result(client, "", "", "malformed", "empty payload");
        return;
    }

    if (payload_len >= 160) {
        ESP_LOGW(TAG, "Ignoring oversized MQTT command payload: len=%d", payload_len);
        publish_command_result(client, "", "", "rejected", "payload too large");
        return;
    }

    char payload_buffer[160];
    memcpy(payload_buffer, payload, payload_len);
    payload_buffer[payload_len] = '\0';

    char command[48];
    char command_id[48] = "";
    if (payload_equals(payload_buffer, "status_refresh")) {
        strncpy(command, "status_refresh", sizeof(command));
        command[sizeof(command) - 1] = '\0';
    } else if (!extract_json_string_field(payload_buffer, "command", command, sizeof(command))) {
        ESP_LOGW(TAG, "Ignoring malformed MQTT command payload");
        publish_command_result(client, "", "", "malformed", "missing or invalid command");
        return;
    } else {
        (void)extract_json_string_field(payload_buffer, "id", command_id, sizeof(command_id));
    }

    if (strcmp(command, "status_refresh") == 0) {
        ESP_LOGI(TAG, "MQTT command received: status_refresh");
        publish_device_status(client);
        publish_device_state(client, true);
        publish_home_assistant_discovery(client);
        publish_command_result(client, command_id, command, "accepted", "status refreshed");
    } else if (strcmp(command, "ui_refresh") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ui_refresh");
        if (mqtt_runtime != NULL) {
            hapanel_runtime_request_refresh(mqtt_runtime);
        }
        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "ui refresh requested");
    } else {
        ESP_LOGW(TAG, "Ignoring unknown MQTT command: %s", command);
        publish_command_result(client, command_id, command, "rejected", "unknown command");
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
        mqtt_connected = true;
        esp_mqtt_client_publish(event->client,
                                CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC,
                                "online",
                                0,
                                0,
                                1);
        subscribe_command_topic(event->client);
        publish_device_status(event->client);
        publish_device_state(event->client, true);
        publish_home_assistant_discovery(event->client);
        set_mqtt_status(CONFIG_HAPANEL_MQTT_BROKER_URI, HAPANEL_SYSTEM_LEVEL_OK);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        mqtt_connected = false;
        set_mqtt_status("Disconnected", HAPANEL_SYSTEM_LEVEL_OFFLINE);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        set_mqtt_status("Error", HAPANEL_SYSTEM_LEVEL_WARNING);
        break;
    case MQTT_EVENT_DATA:
        if (topic_matches(event, CONFIG_HAPANEL_MQTT_COMMAND_TOPIC)) {
            if (event->current_data_offset != 0 || event->total_data_len != event->data_len) {
                ESP_LOGW(TAG,
                         "Ignoring fragmented MQTT command payload: offset=%d total=%d len=%d",
                         event->current_data_offset,
                         event->total_data_len,
                         event->data_len);
                publish_command_result(event->client, "", "", "rejected", "fragmented payload");
                break;
            }
            handle_command_payload(event->client, event->data, event->data_len);
        } else {
            ESP_LOGD(TAG,
                     "MQTT data received: topic_len=%d data_len=%d",
                     event->topic_len,
                     event->data_len);
        }
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
    hapanel_runtime_set_status_callback(runtime, status_change_callback, NULL);

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
