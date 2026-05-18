#include "hapanel_mqtt.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hapanel_ota.h"
#include "hapanel_profile.h"
#include "mqtt_client.h"

#ifndef CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE
#define CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE 0
#endif

static const char *TAG = "hapanel_mqtt";

static hapanel_runtime_t *mqtt_runtime;
static esp_mqtt_client_handle_t mqtt_client;
static bool event_handlers_registered;
static bool mqtt_started;
static bool mqtt_connected;
static bool ota_update_task_running;
static uint32_t published_state_revision;

enum {
    HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE = 4096,
};

typedef struct {
    char command_id[48];
    char url[256];
} ota_update_task_context_t;

typedef struct {
    const char *topic;
    hapanel_home_entity_id_t entity;
} home_topic_subscription_t;

static bool has_text(const char *value);

static const home_topic_subscription_t HOME_TOPIC_SUBSCRIPTIONS[] = {
    {
        .topic = CONFIG_HAPANEL_MQTT_HOME_SCENE_TOPIC,
        .entity = HAPANEL_HOME_ENTITY_SCENE,
    },
    {
        .topic = CONFIG_HAPANEL_MQTT_HOME_LIGHTS_TOPIC,
        .entity = HAPANEL_HOME_ENTITY_LIGHTS,
    },
    {
        .topic = CONFIG_HAPANEL_MQTT_HOME_CLIMATE_TOPIC,
        .entity = HAPANEL_HOME_ENTITY_CLIMATE,
    },
};

static bool topic_matches(const esp_mqtt_event_handle_t event, const char *topic)
{
    return event->topic_len == (int)strlen(topic) &&
           strncmp(event->topic, topic, event->topic_len) == 0;
}

static const home_topic_subscription_t *home_subscription_for_event(
    const esp_mqtt_event_handle_t event)
{
    for (size_t i = 0; i < sizeof(HOME_TOPIC_SUBSCRIPTIONS) / sizeof(HOME_TOPIC_SUBSCRIPTIONS[0]);
         ++i) {
        if (has_text(HOME_TOPIC_SUBSCRIPTIONS[i].topic) &&
            topic_matches(event, HOME_TOPIC_SUBSCRIPTIONS[i].topic)) {
            return &HOME_TOPIC_SUBSCRIPTIONS[i];
        }
    }

    return NULL;
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

static const char *ui_page_name(hapanel_ui_page_id_t page)
{
    const hapanel_ui_page_descriptor_t *descriptor = hapanel_ui_page_descriptor(page);
    return descriptor != NULL && descriptor->name != NULL ? descriptor->name : "unknown";
}

static const char *ui_layer_name(hapanel_ui_layer_t layer)
{
    switch (layer) {
    case HAPANEL_UI_LAYER_AMBIENT:
        return "ambient";
    case HAPANEL_UI_LAYER_ROOT:
        return "root";
    case HAPANEL_UI_LAYER_DOMAIN:
        return "domain";
    case HAPANEL_UI_LAYER_DETAIL:
        return "detail";
    case HAPANEL_UI_LAYER_OVERLAY:
        return "overlay";
    case HAPANEL_UI_LAYER_MODAL:
        return "modal";
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

static bool append_ui_state_object(char *buffer,
                                   size_t buffer_size,
                                   size_t *offset,
                                   const hapanel_runtime_t *runtime)
{
    if (runtime == NULL) {
        return append_text(buffer,
                           buffer_size,
                           offset,
                           "\"ui\":{\"requested\":\"unknown\",\"rendered\":\"unknown\","
                           "\"layer\":\"unknown\"},");
    }

    char requested[32];
    char rendered[32];
    json_escape(ui_page_name(runtime->requested_page), requested, sizeof(requested));
    json_escape(ui_page_name(runtime->rendered_page), rendered, sizeof(rendered));

    const hapanel_ui_page_descriptor_t *descriptor =
        hapanel_ui_page_descriptor(runtime->rendered_page);
    const hapanel_ui_layer_t layer =
        descriptor != NULL ? descriptor->layer : HAPANEL_UI_LAYER_ROOT;

    return append_text(buffer,
                       buffer_size,
                       offset,
                       "\"ui\":{\"requested\":\"%s\",\"rendered\":\"%s\",\"layer\":\"%s\"},",
                       requested,
                       rendered,
                       ui_layer_name(layer));
}

static bool append_home_state_object(char *buffer,
                                     size_t buffer_size,
                                     size_t *offset,
                                     const hapanel_runtime_t *runtime)
{
    if (runtime == NULL) {
        return append_text(buffer, buffer_size, offset, "\"home\":{\"entities\":[]},");
    }

    if (!append_text(buffer,
                     buffer_size,
                     offset,
                     "\"home\":{\"revision\":%" PRIu32 ",\"entities\":[",
                     runtime->home_state.revision)) {
        return false;
    }

    for (size_t i = 0; i < HAPANEL_HOME_ENTITY_COUNT; ++i) {
        const hapanel_home_entity_t *entity = &runtime->home_state.entities[i];
        char label[HAPANEL_HOME_ENTITY_LABEL_MAX * 2];
        char value[HAPANEL_HOME_ENTITY_VALUE_MAX * 2];
        json_escape(entity->label, label, sizeof(label));
        json_escape(entity->value, value, sizeof(value));

        if (!append_text(buffer,
                         buffer_size,
                         offset,
                         "%s{\"label\":\"%s\",\"value\":\"%s\",\"online\":%s,"
                         "\"revision\":%" PRIu32 "}",
                         i == 0 ? "" : ",",
                         label,
                         value,
                         entity->online ? "true" : "false",
                         entity->revision)) {
            return false;
        }
    }

    return append_text(buffer, buffer_size, offset, "]},");
}

static bool append_ota_state_object(char *buffer,
                                    size_t buffer_size,
                                    size_t *offset,
                                    const hapanel_system_status_item_t *item)
{
    hapanel_ota_preflight_t preflight = {0};
    (void)hapanel_ota_preflight(&preflight);
    hapanel_ota_inventory_t inventory = {0};
    (void)hapanel_ota_get_inventory(&inventory);
    hapanel_ota_progress_t progress = {0};
    (void)hapanel_ota_get_progress(&progress);

    char value[128];
    char reason[96];
    char running_label[32];
    char target_label[32];
    char running_state[32];
    char phase[32];
    char progress_phase[32];
    char progress_target[32];
    json_escape(item->value, value, sizeof(value));
    json_escape(preflight.reason != NULL ? preflight.reason : "unknown", reason, sizeof(reason));
    json_escape(preflight.running_label != NULL ? preflight.running_label : "none",
                running_label,
                sizeof(running_label));
    json_escape(preflight.target_label != NULL ? preflight.target_label : "none",
                target_label,
                sizeof(target_label));
    json_escape(inventory.running_state != NULL ? inventory.running_state : "unknown",
                running_state,
                sizeof(running_state));
    json_escape(inventory.phase != NULL ? inventory.phase : "unknown", phase, sizeof(phase));
    json_escape(progress.phase != NULL ? progress.phase : "unknown",
                progress_phase,
                sizeof(progress_phase));
    json_escape(progress.target_label[0] != '\0' ? progress.target_label : "none",
                progress_target,
                sizeof(progress_target));

    return append_text(buffer,
                       buffer_size,
                       offset,
                       "\"ota\":{\"value\":\"%s\",\"level\":\"%s\","
                       "\"preflight\":{\"allowed\":%s,\"reason\":\"%s\","
                       "\"running\":\"%s\",\"target\":\"%s\","
                       "\"target_address\":%" PRIu32 ",\"target_size\":%" PRIu32 "},"
                       "\"progress\":{\"active\":%s,\"phase\":\"%s\",\"target\":\"%s\","
                       "\"written\":%zu,\"expected\":%zu,\"percent\":%u},"
                       "\"inventory\":{\"boot_matches_running\":%s,\"reboot_required\":%s,"
                       "\"rollback_enabled\":%s,\"running_state\":\"%s\",\"phase\":\"%s\","
                       "\"running\":{\"present\":%s,\"label\":\"%s\",\"address\":%" PRIu32
                       ",\"size\":%" PRIu32 "},"
                       "\"boot\":{\"present\":%s,\"label\":\"%s\",\"address\":%" PRIu32
                       ",\"size\":%" PRIu32 "},"
                       "\"factory\":{\"present\":%s,\"label\":\"%s\",\"address\":%" PRIu32
                       ",\"size\":%" PRIu32 "},"
                       "\"ota_0\":{\"present\":%s,\"label\":\"%s\",\"address\":%" PRIu32
                       ",\"size\":%" PRIu32 "},"
                       "\"ota_1\":{\"present\":%s,\"label\":\"%s\",\"address\":%" PRIu32
                       ",\"size\":%" PRIu32 "}}},",
                       value,
                       system_level_name(item->level),
                       preflight.allowed ? "true" : "false",
                       reason,
                       running_label,
                       target_label,
                       preflight.target_address,
                       preflight.target_size,
                       progress.active ? "true" : "false",
                       progress_phase,
                       progress_target,
                       progress.written_size,
                       progress.expected_size,
                       progress.percent,
                       inventory.boot_matches_running ? "true" : "false",
                       inventory.reboot_required ? "true" : "false",
                       inventory.rollback_enabled ? "true" : "false",
                       running_state,
                       phase,
                       inventory.running.present ? "true" : "false",
                       inventory.running.label,
                       inventory.running.address,
                       inventory.running.size,
                       inventory.boot.present ? "true" : "false",
                       inventory.boot.label,
                       inventory.boot.address,
                       inventory.boot.size,
                       inventory.factory.present ? "true" : "false",
                       inventory.factory.label,
                       inventory.factory.address,
                       inventory.factory.size,
                       inventory.ota_0.present ? "true" : "false",
                       inventory.ota_0.label,
                       inventory.ota_0.address,
                       inventory.ota_0.size,
                       inventory.ota_1.present ? "true" : "false",
                       inventory.ota_1.label,
                       inventory.ota_1.address,
                       inventory.ota_1.size);
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

    char *payload = malloc(HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE);
    if (payload == NULL) {
        ESP_LOGW(TAG, "Failed to allocate MQTT device state payload");
        return;
    }

    size_t offset = 0;
    if (!append_text(payload,
                     HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                     &offset,
                     "{\"schema\":\"hapanel.device_state.v1\","
                     "\"revision\":%" PRIu32 ","
                     "\"uptime_ms\":%" PRIu64 ","
                     "\"psram_ready\":%s,",
                     status->revision,
                     (uint64_t)(esp_timer_get_time() / 1000),
                     status->psram_ready ? "true" : "false")) {
        ESP_LOGW(TAG, "MQTT device state payload header truncated; skipping publish");
        goto cleanup;
    }

    if (!append_ui_state_object(payload,
                                HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                                &offset,
                                mqtt_runtime)) {
        ESP_LOGW(TAG, "MQTT device state UI payload truncated; skipping publish");
        goto cleanup;
    }

    if (!append_home_state_object(payload,
                                  HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                                  &offset,
                                  mqtt_runtime)) {
        ESP_LOGW(TAG, "MQTT device state home payload truncated; skipping publish");
        goto cleanup;
    }

    if (!append_status_item_object(payload,
                                   HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                                   &offset,
                                   "wifi",
                                   &status->items[HAPANEL_SYSTEM_WIFI]) ||
        !append_status_item_object(payload,
                                   HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                                   &offset,
                                   "mqtt",
                                   &status->items[HAPANEL_SYSTEM_MQTT]) ||
        !append_ota_state_object(payload,
                                 HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                                 &offset,
                                 &status->items[HAPANEL_SYSTEM_OTA])) {
        ESP_LOGW(TAG, "MQTT device state connectivity payload truncated; skipping publish");
        goto cleanup;
    }

    if (!append_text(payload, HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE, &offset, "\"services\":[")) {
        ESP_LOGW(TAG, "MQTT device state services header truncated; skipping publish");
        goto cleanup;
    }

    for (size_t i = 0; i < status->item_count; ++i) {
        char label[64];
        char value[128];
        json_escape(status->items[i].label, label, sizeof(label));
        json_escape(status->items[i].value, value, sizeof(value));

        if (!append_text(payload,
                         HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE,
                         &offset,
                         "%s{\"label\":\"%s\",\"value\":\"%s\",\"level\":\"%s\"}",
                         i == 0 ? "" : ",",
                         label,
                         value,
                         system_level_name(status->items[i].level))) {
            ESP_LOGW(TAG, "MQTT device state payload truncated; skipping publish");
            goto cleanup;
        }
    }

    if (!append_text(payload, HAPANEL_MQTT_DEVICE_STATE_PAYLOAD_SIZE, &offset, "]}")) {
        ESP_LOGW(TAG, "MQTT device state payload footer truncated; skipping publish");
        goto cleanup;
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

cleanup:
    free(payload);
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
    char command_result_topic[128];
    char command_state_topic[128];
    char availability_topic[128];

    json_escape(app->version, app_version, sizeof(app_version));
    json_escape(profile->board.name, board_name, sizeof(board_name));
    json_escape(CONFIG_HAPANEL_MQTT_CLIENT_ID, client_id, sizeof(client_id));
    json_escape(CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC, status_topic, sizeof(status_topic));
    json_escape(CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC, state_topic, sizeof(state_topic));
    json_escape(CONFIG_HAPANEL_MQTT_COMMAND_TOPIC, command_topic, sizeof(command_topic));
    json_escape(CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC,
                command_result_topic,
                sizeof(command_result_topic));
    json_escape(CONFIG_HAPANEL_MQTT_COMMAND_STATE_TOPIC,
                command_state_topic,
                sizeof(command_state_topic));
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
            .component = "sensor",
            .object_id = "hapanel_ota_phase",
            .payload_format =
                "{\"name\":\"OTA Phase\","
                "\"unique_id\":\"%s_ota_phase\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.ota.inventory.phase }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "binary_sensor",
            .object_id = "hapanel_ota_ready",
            .payload_format =
                "{\"name\":\"OTA Ready\","
                "\"unique_id\":\"%s_ota_ready\","
                "\"entity_category\":\"diagnostic\","
                "\"device_class\":\"connectivity\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ 'ON' if value_json.ota.preflight.allowed else 'OFF' }}\","
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
            .object_id = "hapanel_ota_running_slot",
            .payload_format =
                "{\"name\":\"OTA Running Slot\","
                "\"unique_id\":\"%s_ota_running_slot\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.ota.preflight.running }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "sensor",
            .object_id = "hapanel_ota_target_slot",
            .payload_format =
                "{\"name\":\"OTA Target Slot\","
                "\"unique_id\":\"%s_ota_target_slot\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.ota.preflight.target }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = state_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "sensor",
            .object_id = "hapanel_last_command_result",
            .payload_format =
                "{\"name\":\"Last Command Result\","
                "\"unique_id\":\"%s_last_command_result\","
                "\"entity_category\":\"diagnostic\","
                "\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.status }}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_state_topic,
            .attributes_topic = command_state_topic,
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
        {
            .component = "button",
            .object_id = "hapanel_ui_show_root",
            .payload_format =
                "{\"name\":\"Show Root Page\","
                "\"unique_id\":\"%s_ui_show_root\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ui_show_root\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ui_show_home",
            .payload_format =
                "{\"name\":\"Show Home Page\","
                "\"unique_id\":\"%s_ui_show_home\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ui_show_home\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ui_show_status",
            .payload_format =
                "{\"name\":\"Show Status Page\","
                "\"unique_id\":\"%s_ui_show_status\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ui_show_status\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ui_show_security",
            .payload_format =
                "{\"name\":\"Show Security Page\","
                "\"unique_id\":\"%s_ui_show_security\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ui_show_security\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ui_show_apps",
            .payload_format =
                "{\"name\":\"Show Apps Page\","
                "\"unique_id\":\"%s_ui_show_apps\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ui_show_apps\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ota_preflight",
            .payload_format =
                "{\"name\":\"Check OTA Readiness\","
                "\"unique_id\":\"%s_ota_preflight\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ota_preflight\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = command_state_topic,
        },
        {
            .component = "button",
            .object_id = "hapanel_ota_reboot",
            .payload_format =
                "{\"name\":\"Reboot to Staged OTA\","
                "\"unique_id\":\"%s_ota_reboot\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ota_reboot\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = state_topic,
        },
#if CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE
        {
            .component = "button",
            .object_id = "hapanel_ota_self_test_stage",
            .payload_format =
                "{\"name\":\"Stage OTA Self-Test\","
                "\"unique_id\":\"%s_ota_self_test_stage\","
                "\"entity_category\":\"diagnostic\","
                "\"command_topic\":\"%s\","
                "\"payload_press\":\"{\\\"command\\\":\\\"ota_self_test_stage\\\"}\","
                "\"availability_topic\":\"%s\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\","
                "\"json_attributes_topic\":\"%s\",%s}",
            .topic = command_topic,
            .attributes_topic = command_state_topic,
        },
#endif
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

#if !CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE
    char self_test_topic[160];
    const int self_test_topic_len = snprintf(self_test_topic,
                                             sizeof(self_test_topic),
                                             "%s/button/hapanel_ota_self_test_stage/config",
                                             CONFIG_HAPANEL_MQTT_HA_DISCOVERY_PREFIX);
    if (self_test_topic_len < 0 || self_test_topic_len >= (int)sizeof(self_test_topic)) {
        ESP_LOGW(TAG, "Home Assistant discovery cleanup topic truncated; skipping publish");
    } else {
        const int msg_id = esp_mqtt_client_publish(client, self_test_topic, "", 0, 0, 1);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "Failed to clear Home Assistant discovery: topic=%s", self_test_topic);
        } else {
            ESP_LOGI(TAG,
                     "Cleared Home Assistant discovery: topic=%s msg_id=%d",
                     self_test_topic,
                     msg_id);
        }
    }
#endif
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

    const int result_msg_id = esp_mqtt_client_publish(client,
                                                      CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC,
                                                      payload,
                                                      payload_len,
                                                      0,
                                                      0);
    if (result_msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish MQTT command result");
    } else {
        ESP_LOGI(TAG,
                 "Published MQTT command result: topic=%s command=%s status=%s msg_id=%d",
                 CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC,
                 command_json,
                 status_json,
                 result_msg_id);
    }

    const int state_msg_id = esp_mqtt_client_publish(client,
                                                     CONFIG_HAPANEL_MQTT_COMMAND_STATE_TOPIC,
                                                     payload,
                                                     payload_len,
                                                     0,
                                                     1);
    if (state_msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish MQTT command state");
    } else {
        ESP_LOGI(TAG,
                 "Published MQTT command state: topic=%s command=%s status=%s msg_id=%d",
                 CONFIG_HAPANEL_MQTT_COMMAND_STATE_TOPIC,
                 command_json,
                 status_json,
                 state_msg_id);
    }
}

static void status_change_callback(void *context)
{
    (void)context;
    publish_device_state(mqtt_client, false);
}

static void ota_update_task(void *context)
{
    ota_update_task_context_t *task_context = (ota_update_task_context_t *)context;
    if (task_context == NULL) {
        ota_update_task_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting MQTT-requested OTA update from URL");
    esp_err_t update_result = hapanel_ota_install_from_http_url(mqtt_runtime, task_context->url);
    publish_device_state(mqtt_client, true);
    publish_command_result(mqtt_client,
                           task_context->command_id,
                           "ota_update",
                           update_result == ESP_OK ? "accepted" : "rejected",
                           update_result == ESP_OK ? "staged; reboot needed"
                                                   : esp_err_to_name(update_result));

    free(task_context);
    ota_update_task_running = false;
    vTaskDelete(NULL);
}

static void ota_reboot_task(void *context)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(750));
    ESP_LOGW(TAG, "Restarting to boot staged OTA image");
    esp_restart();
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

static void subscribe_home_topics(esp_mqtt_client_handle_t client)
{
    for (size_t i = 0; i < sizeof(HOME_TOPIC_SUBSCRIPTIONS) / sizeof(HOME_TOPIC_SUBSCRIPTIONS[0]);
         ++i) {
        const char *topic = HOME_TOPIC_SUBSCRIPTIONS[i].topic;
        if (!has_text(topic)) {
            continue;
        }

        const int msg_id = esp_mqtt_client_subscribe(client, topic, 0);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "Failed to subscribe Home tile topic: %s", topic);
        } else {
            ESP_LOGI(TAG, "Subscribed Home tile topic: topic=%s msg_id=%d", topic, msg_id);
        }
    }
}

static void trim_payload_text(char *value)
{
    if (value == NULL) {
        return;
    }

    char *start = (char *)skip_ascii_space(value);
    if (start != value) {
        memmove(value, start, strlen(start) + 1);
    }

    size_t len = strlen(value);
    while (len > 0 && is_ascii_space(value[len - 1])) {
        value[--len] = '\0';
    }
}

static void handle_home_topic_payload(esp_mqtt_client_handle_t client,
                                      const home_topic_subscription_t *subscription,
                                      const char *payload,
                                      int payload_len)
{
    if (mqtt_runtime == NULL || subscription == NULL || payload_len < 0) {
        return;
    }

    char value[HAPANEL_HOME_ENTITY_VALUE_MAX];
    const size_t copy_len = payload_len < (int)(sizeof(value) - 1)
                                ? (size_t)payload_len
                                : sizeof(value) - 1;
    memcpy(value, payload, copy_len);
    value[copy_len] = '\0';
    trim_payload_text(value);

    const bool online = value[0] != '\0' && strcmp(value, "unavailable") != 0 &&
                        strcmp(value, "unknown") != 0 && strcmp(value, "offline") != 0;
    hapanel_runtime_set_home_entity(mqtt_runtime, subscription->entity, value, online);
    publish_device_state(client, true);
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

    if (payload_len >= 512) {
        ESP_LOGW(TAG, "Ignoring oversized MQTT command payload: len=%d", payload_len);
        publish_command_result(client, "", "", "rejected", "payload too large");
        return;
    }

    char payload_buffer[512];
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
    } else if (strcmp(command, "ui_show_root") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ui_show_root");
        if (mqtt_runtime != NULL) {
            hapanel_runtime_show_page(mqtt_runtime, HAPANEL_UI_PAGE_ROOT);
        }
        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "root page requested");
    } else if (strcmp(command, "ui_show_home") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ui_show_home");
        if (mqtt_runtime != NULL) {
            hapanel_runtime_show_page(mqtt_runtime, HAPANEL_UI_PAGE_HOME);
        }
        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "home page requested");
    } else if (strcmp(command, "ui_show_status") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ui_show_status");
        if (mqtt_runtime != NULL) {
            hapanel_runtime_show_page(mqtt_runtime, HAPANEL_UI_PAGE_SYSTEM_STATUS);
        }
        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "status page requested");
    } else if (strcmp(command, "ui_show_security") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ui_show_security");
        if (mqtt_runtime != NULL) {
            hapanel_runtime_show_page(mqtt_runtime, HAPANEL_UI_PAGE_SECURITY);
        }
        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "security page requested");
    } else if (strcmp(command, "ui_show_apps") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ui_show_apps");
        if (mqtt_runtime != NULL) {
            hapanel_runtime_show_page(mqtt_runtime, HAPANEL_UI_PAGE_APPS);
        }
        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "apps page requested");
    } else if (strcmp(command, "ota_preflight") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ota_preflight");
        hapanel_ota_preflight_t preflight = {0};
        esp_err_t preflight_result = hapanel_ota_preflight(&preflight);
        publish_device_state(client, true);

        char reason[96];
        const int reason_len = snprintf(reason,
                                        sizeof(reason),
                                        "%s: running=%s target=%s size=%" PRIu32,
                                        preflight.reason != NULL ? preflight.reason : "unknown",
                                        preflight.running_label != NULL ? preflight.running_label
                                                                        : "none",
                                        preflight.target_label != NULL ? preflight.target_label
                                                                       : "none",
                                        preflight.target_size);
        if (reason_len < 0 || reason_len >= (int)sizeof(reason)) {
            strncpy(reason,
                    preflight.allowed ? "ready" : "preflight blocked",
                    sizeof(reason));
            reason[sizeof(reason) - 1] = '\0';
        }

        publish_command_result(client,
                               command_id,
                               command,
                               preflight_result == ESP_OK && preflight.allowed ? "accepted"
                                                                               : "rejected",
                               reason);
    } else if (strcmp(command, "ota_self_test_stage") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ota_self_test_stage");
        if (!CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE) {
            publish_command_result(client,
                                   command_id,
                                   command,
                                   "rejected",
                                   "self-test staging disabled");
            return;
        }

        if (mqtt_runtime == NULL) {
            publish_command_result(client, command_id, command, "rejected", "runtime unavailable");
            return;
        }

        esp_err_t stage_result = hapanel_ota_self_test_stage_any_running(mqtt_runtime);
        publish_device_state(client, true);
        publish_command_result(client,
                               command_id,
                               command,
                               stage_result == ESP_OK ? "accepted" : "rejected",
                               stage_result == ESP_OK ? "staged; reboot needed"
                                                      : esp_err_to_name(stage_result));
    } else if (strcmp(command, "ota_update") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ota_update");
        if (mqtt_runtime == NULL) {
            publish_command_result(client, command_id, command, "rejected", "runtime unavailable");
            return;
        }

        if (ota_update_task_running) {
            publish_command_result(client, command_id, command, "rejected", "ota already running");
            return;
        }

        char url[256];
        if (!extract_json_string_field(payload_buffer, "url", url, sizeof(url))) {
            publish_command_result(client, command_id, command, "malformed", "missing or invalid url");
            return;
        }

        ota_update_task_context_t *task_context = calloc(1, sizeof(*task_context));
        if (task_context == NULL) {
            publish_command_result(client, command_id, command, "rejected", "no memory");
            return;
        }

        snprintf(task_context->command_id, sizeof(task_context->command_id), "%s", command_id);
        snprintf(task_context->url, sizeof(task_context->url), "%s", url);
        ota_update_task_running = true;

        BaseType_t task_result = xTaskCreate(ota_update_task,
                                             "hapanel_ota_update",
                                             8192,
                                             task_context,
                                             5,
                                             NULL);
        if (task_result != pdPASS) {
            ota_update_task_running = false;
            free(task_context);
            publish_command_result(client, command_id, command, "rejected", "task create failed");
            return;
        }

        publish_command_result(client, command_id, command, "accepted", "ota update started");
    } else if (strcmp(command, "ota_reboot") == 0) {
        ESP_LOGI(TAG, "MQTT command received: ota_reboot");
        hapanel_ota_inventory_t inventory = {0};
        esp_err_t inventory_result = hapanel_ota_get_inventory(&inventory);
        if (inventory_result != ESP_OK || !inventory.reboot_required) {
            publish_device_state(client, true);
            publish_command_result(client,
                                   command_id,
                                   command,
                                   "rejected",
                                   "no staged ota image");
            return;
        }

        BaseType_t task_result = xTaskCreate(ota_reboot_task,
                                             "hapanel_ota_reboot",
                                             2048,
                                             NULL,
                                             5,
                                             NULL);
        if (task_result != pdPASS) {
            publish_command_result(client, command_id, command, "rejected", "task create failed");
            return;
        }

        publish_device_state(client, true);
        publish_command_result(client, command_id, command, "accepted", "rebooting to staged ota");
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
        subscribe_home_topics(event->client);
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
            const home_topic_subscription_t *subscription = home_subscription_for_event(event);
            if (subscription != NULL) {
                if (event->current_data_offset != 0 || event->total_data_len != event->data_len) {
                    ESP_LOGW(TAG,
                             "Ignoring fragmented Home tile payload: offset=%d total=%d len=%d",
                             event->current_data_offset,
                             event->total_data_len,
                             event->data_len);
                    break;
                }
                handle_home_topic_payload(event->client,
                                          subscription,
                                          event->data,
                                          event->data_len);
            } else {
                ESP_LOGD(TAG,
                         "MQTT data received: topic_len=%d data_len=%d",
                         event->topic_len,
                         event->data_len);
            }
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
