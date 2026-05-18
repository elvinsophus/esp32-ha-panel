#include "hapanel_ota.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "sdkconfig.h"

static const char *TAG = "hapanel_ota";
static hapanel_ota_progress_t ota_progress = {
    .phase = "idle",
};

enum {
    HAPANEL_OTA_COPY_CHUNK_SIZE = 4096,
    HAPANEL_OTA_HTTP_CHUNK_SIZE = 4096,
    HAPANEL_OTA_PROGRESS_STEP_PERCENT = 10,
};

static void log_partition(const char *role, const esp_partition_t *partition)
{
    if (partition == NULL) {
        ESP_LOGW(TAG, "%s partition: unavailable", role);
        return;
    }

    ESP_LOGI(TAG,
             "%s partition: label=%s subtype=0x%02x address=0x%06" PRIx32 " size=0x%06" PRIx32,
             role,
             partition->label,
             partition->subtype,
             partition->address,
             partition->size);
}

static void copy_partition_info(hapanel_ota_partition_info_t *info,
                                const esp_partition_t *partition)
{
    if (info == NULL) {
        return;
    }

    *info = (hapanel_ota_partition_info_t){0};
    if (partition == NULL) {
        return;
    }

    info->present = true;
    const size_t label_len = strnlen(partition->label, sizeof(info->label) - 1);
    memcpy(info->label, partition->label, label_len);
    info->label[label_len] = '\0';
    info->subtype = partition->subtype;
    info->address = partition->address;
    info->size = partition->size;
}

static void set_ota_status(hapanel_runtime_t *runtime,
                           const char *value,
                           hapanel_system_level_t level)
{
    if (runtime == NULL) {
        return;
    }

    hapanel_runtime_set_status(runtime, HAPANEL_SYSTEM_OTA, value, level);
}

static void set_progress_phase(const char *phase, bool active)
{
    ota_progress.phase = phase != NULL ? phase : "unknown";
    ota_progress.active = active;
}

static void reset_progress(void)
{
    ota_progress = (hapanel_ota_progress_t){
        .active = false,
        .phase = "idle",
    };
}

static void update_progress_from_session(const hapanel_ota_session_t *session)
{
    if (session == NULL) {
        return;
    }

    ota_progress.active = session->active;
    ota_progress.written_size = session->written_size;
    ota_progress.expected_size = session->expected_size;
    ota_progress.percent = 0;
    if (session->expected_size > 0) {
        ota_progress.percent =
            (uint8_t)((session->written_size * 100U) / session->expected_size);
        if (ota_progress.percent > 100) {
            ota_progress.percent = 100;
        }
    }

    if (session->target != NULL) {
        const size_t label_len = strnlen(session->target->label,
                                         sizeof(ota_progress.target_label) - 1);
        memcpy(ota_progress.target_label, session->target->label, label_len);
        ota_progress.target_label[label_len] = '\0';
    }
}

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending_verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default:
        return "undefined";
    }
}

static esp_err_t get_running_ota_state(const esp_partition_t *running,
                                       esp_ota_img_states_t *state)
{
    if (running == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_ota_get_state_partition(running, state);
}

static void set_preflight_result(hapanel_ota_preflight_t *preflight,
                                 bool allowed,
                                 const char *reason,
                                 const esp_partition_t *running,
                                 const esp_partition_t *target)
{
    preflight->allowed = allowed;
    preflight->reason = reason;
    preflight->running_label = running != NULL ? running->label : NULL;
    preflight->target_label = target != NULL ? target->label : NULL;
    preflight->target_address = target != NULL ? target->address : 0;
    preflight->target_size = target != NULL ? target->size : 0;
}

static void log_preflight(const hapanel_ota_preflight_t *preflight)
{
    ESP_LOGI(TAG,
             "OTA preflight: allowed=%s reason=%s running=%s target=%s address=0x%06" PRIx32
             " size=0x%06" PRIx32,
             preflight->allowed ? "yes" : "no",
             preflight->reason,
             preflight->running_label != NULL ? preflight->running_label : "none",
             preflight->target_label != NULL ? preflight->target_label : "none",
             preflight->target_address,
             preflight->target_size);
}

esp_err_t hapanel_ota_preflight(hapanel_ota_preflight_t *preflight)
{
    if (preflight == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *preflight = (hapanel_ota_preflight_t){0};

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        set_preflight_result(preflight, false, "no running partition", NULL, NULL);
        log_preflight(preflight);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        set_preflight_result(preflight, false, "no update slot", running, NULL);
        log_preflight(preflight);
        return ESP_ERR_NOT_FOUND;
    }

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_result = get_running_ota_state(running, &running_state);
    if (state_result == ESP_OK && running_state == ESP_OTA_IMG_PENDING_VERIFY) {
        set_preflight_result(preflight, false, "running image pending verification", running, target);
        log_preflight(preflight);
        return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
    }

    if (state_result != ESP_OK && state_result != ESP_ERR_NOT_SUPPORTED &&
        state_result != ESP_ERR_NOT_FOUND) {
        set_preflight_result(preflight, false, "running state unavailable", running, target);
        log_preflight(preflight);
        return state_result;
    }
#endif

    set_preflight_result(preflight, true, "ready", running, target);
    log_preflight(preflight);
    return ESP_OK;
}

esp_err_t hapanel_ota_get_inventory(hapanel_ota_inventory_t *inventory)
{
    if (inventory == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *inventory = (hapanel_ota_inventory_t){
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        .rollback_enabled = true,
#else
        .rollback_enabled = false,
#endif
        .running_state = "unavailable",
        .phase = "unknown",
    };

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *ota_data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_DATA_OTA,
                                                               NULL);
    const esp_partition_t *boot = ota_data != NULL ? esp_ota_get_boot_partition() : running;
    const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                              ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                              NULL);
    const esp_partition_t *ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                            NULL);
    const esp_partition_t *ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                            NULL);
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);

    copy_partition_info(&inventory->running, running);
    copy_partition_info(&inventory->boot, boot);
    copy_partition_info(&inventory->factory, factory);
    copy_partition_info(&inventory->ota_0, ota_0);
    copy_partition_info(&inventory->ota_1, ota_1);
    copy_partition_info(&inventory->target, target);
    inventory->boot_matches_running = running != NULL && boot != NULL && running == boot;
    inventory->reboot_required = running != NULL && boot != NULL && running != boot;

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    if (running != NULL) {
        esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
        esp_err_t state_result = get_running_ota_state(running, &running_state);
        if (state_result == ESP_OK) {
            inventory->running_state = ota_state_name(running_state);
        } else if (state_result == ESP_ERR_NOT_SUPPORTED || state_result == ESP_ERR_NOT_FOUND) {
            inventory->running_state = "undefined";
        } else {
            inventory->running_state = "unknown";
        }
    }
#else
    inventory->running_state = "rollback_disabled";
#endif

    if (running == NULL || boot == NULL) {
        inventory->phase = "partition_error";
    } else if (inventory->reboot_required) {
        inventory->phase = "reboot_needed";
    } else if (strcmp(inventory->running_state, "pending_verify") == 0 ||
               strcmp(inventory->running_state, "new") == 0) {
        inventory->phase = "pending_verify";
    } else if (strcmp(inventory->running_state, "invalid") == 0 ||
               strcmp(inventory->running_state, "aborted") == 0) {
        inventory->phase = "invalid";
    } else {
        inventory->phase = "ready";
    }

    return running != NULL && boot != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t hapanel_ota_get_progress(hapanel_ota_progress_t *progress)
{
    if (progress == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *progress = ota_progress;
    if (progress->phase == NULL) {
        progress->phase = "unknown";
    }
    return ESP_OK;
}

esp_err_t hapanel_ota_begin(hapanel_runtime_t *runtime,
                            hapanel_ota_session_t *session,
                            size_t image_size)
{
    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (session->active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (image_size == 0) {
        set_ota_status(runtime, "Invalid image", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_INVALID_SIZE;
    }

    hapanel_ota_preflight_t preflight;
    esp_err_t preflight_result = hapanel_ota_preflight(&preflight);
    if (preflight_result != ESP_OK || !preflight.allowed) {
        set_ota_status(runtime, "Preflight blocked", HAPANEL_SYSTEM_LEVEL_WARNING);
        return preflight_result != ESP_OK ? preflight_result : ESP_FAIL;
    }

    if (image_size > preflight.target_size) {
        ESP_LOGE(TAG,
                 "OTA image is too large: image=%zu target=%" PRIu32,
                 image_size,
                 preflight.target_size);
        set_ota_status(runtime, "Image too large", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        set_ota_status(runtime, "No update slot", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_handle_t handle = 0;
    set_ota_status(runtime, "Preparing", HAPANEL_SYSTEM_LEVEL_PENDING);
    esp_err_t begin_result = esp_ota_begin(target, image_size, &handle);
    if (begin_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA write: %s", esp_err_to_name(begin_result));
        set_ota_status(runtime, "Begin failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        return begin_result;
    }

    *session = (hapanel_ota_session_t){
        .active = true,
        .handle = handle,
        .target = target,
        .runtime = runtime,
        .expected_size = image_size,
        .written_size = 0,
        .last_progress_percent = 0,
    };

    ESP_LOGI(TAG,
             "OTA write session started: target=%s size=%zu",
             target->label,
             image_size);
    set_progress_phase("writing", true);
    update_progress_from_session(session);
    set_ota_status(runtime, "Receiving", HAPANEL_SYSTEM_LEVEL_PENDING);
    return ESP_OK;
}

esp_err_t hapanel_ota_write(hapanel_ota_session_t *session, const void *data, size_t size)
{
    if (session == NULL || !session->active || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (size == 0) {
        return ESP_OK;
    }

    if (session->written_size > session->expected_size ||
        size > session->expected_size - session->written_size) {
        set_ota_status(session->runtime, "Image too large", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t write_result = esp_ota_write(session->handle, data, size);
    if (write_result != ESP_OK) {
        ESP_LOGE(TAG, "OTA write failed after %zu bytes: %s",
                 session->written_size,
                 esp_err_to_name(write_result));
        set_ota_status(session->runtime, "Write failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        return write_result;
    }

    session->written_size += size;
    update_progress_from_session(session);
    if (session->expected_size > 0) {
        uint8_t percent = (uint8_t)((session->written_size * 100U) / session->expected_size);
        percent = (uint8_t)((percent / HAPANEL_OTA_PROGRESS_STEP_PERCENT) *
                            HAPANEL_OTA_PROGRESS_STEP_PERCENT);
        if (percent > 0 && percent < 100 && percent != session->last_progress_percent) {
            char status[24];
            const int status_len = snprintf(status, sizeof(status), "Writing %u%%", percent);
            if (status_len > 0 && status_len < (int)sizeof(status)) {
                set_ota_status(session->runtime, status, HAPANEL_SYSTEM_LEVEL_PENDING);
            }
            session->last_progress_percent = percent;
        }
    }

    return ESP_OK;
}

esp_err_t hapanel_ota_finish(hapanel_ota_session_t *session)
{
    if (session == NULL || !session->active) {
        return ESP_ERR_INVALID_ARG;
    }

    if (session->written_size != session->expected_size) {
        ESP_LOGE(TAG,
                 "OTA image is incomplete: written=%zu expected=%zu",
                 session->written_size,
                 session->expected_size);
        set_ota_status(session->runtime, "Incomplete", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t end_result = esp_ota_end(session->handle);
    if (end_result != ESP_OK) {
        ESP_LOGE(TAG, "OTA image validation failed: %s", esp_err_to_name(end_result));
        set_ota_status(session->runtime, "Invalid image", HAPANEL_SYSTEM_LEVEL_ERROR);
        memset(session, 0, sizeof(*session));
        return end_result;
    }

    esp_err_t boot_result = esp_ota_set_boot_partition(session->target);
    if (boot_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set OTA boot partition: %s", esp_err_to_name(boot_result));
        set_ota_status(session->runtime, "Boot set failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        memset(session, 0, sizeof(*session));
        return boot_result;
    }

    ESP_LOGI(TAG, "OTA image staged for next boot: target=%s", session->target->label);
    update_progress_from_session(session);
    set_progress_phase("staged", false);
    set_ota_status(session->runtime, "Reboot needed", HAPANEL_SYSTEM_LEVEL_OK);
    memset(session, 0, sizeof(*session));
    return ESP_OK;
}

esp_err_t hapanel_ota_abort(hapanel_ota_session_t *session)
{
    if (session == NULL || !session->active) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t abort_result = esp_ota_abort(session->handle);
    if (abort_result != ESP_OK) {
        ESP_LOGW(TAG, "OTA abort returned: %s", esp_err_to_name(abort_result));
    }

    set_ota_status(session->runtime, "Aborted", HAPANEL_SYSTEM_LEVEL_WARNING);
    set_progress_phase("aborted", false);
    memset(session, 0, sizeof(*session));
    return abort_result;
}

esp_err_t hapanel_ota_install_from_http_url(hapanel_runtime_t *runtime, const char *url)
{
    if (runtime == NULL || url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    reset_progress();
    set_progress_phase("connecting", true);

    if (strncmp(url, "http://", strlen("http://")) != 0) {
        ESP_LOGW(TAG, "Rejecting OTA URL without plain HTTP scheme");
        set_progress_phase("rejected", false);
        set_ota_status(runtime, "Bad URL", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = HAPANEL_OTA_HTTP_CHUNK_SIZE,
    };

    set_ota_status(runtime, "Connecting", HAPANEL_SYSTEM_LEVEL_PENDING);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        set_progress_phase("error", false);
        set_ota_status(runtime, "HTTP failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_FAIL;
    }

    esp_err_t result = esp_http_client_open(client, 0);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open OTA URL: %s", esp_err_to_name(result));
        set_progress_phase("error", false);
        set_ota_status(runtime, "HTTP failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        esp_http_client_cleanup(client);
        return result;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG,
             "OTA HTTP response: status=%d content_length=%" PRId64,
             status_code,
             content_length);

    if (status_code < 200 || status_code >= 300) {
        set_progress_phase("error", false);
        set_ota_status(runtime, "HTTP status", HAPANEL_SYSTEM_LEVEL_ERROR);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (content_length <= 0 || content_length > SIZE_MAX) {
        ESP_LOGE(TAG, "OTA HTTP response must include a known Content-Length");
        set_progress_phase("error", false);
        set_ota_status(runtime, "Size unknown", HAPANEL_SYSTEM_LEVEL_ERROR);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    hapanel_ota_session_t session = {0};
    result = hapanel_ota_begin(runtime, &session, (size_t)content_length);
    if (result != ESP_OK) {
        set_progress_phase("error", false);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return result;
    }

    uint8_t *buffer = malloc(HAPANEL_OTA_HTTP_CHUNK_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Unable to allocate OTA HTTP buffer");
        (void)hapanel_ota_abort(&session);
        set_progress_phase("error", false);
        set_ota_status(runtime, "No memory", HAPANEL_SYSTEM_LEVEL_ERROR);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    while (session.written_size < session.expected_size) {
        const size_t remaining = session.expected_size - session.written_size;
        const int to_read = remaining < HAPANEL_OTA_HTTP_CHUNK_SIZE
                                ? (int)remaining
                                : HAPANEL_OTA_HTTP_CHUNK_SIZE;
        const int read_size = esp_http_client_read(client, (char *)buffer, to_read);
        if (read_size < 0) {
            ESP_LOGE(TAG, "OTA HTTP read failed");
            free(buffer);
            (void)hapanel_ota_abort(&session);
            set_progress_phase("error", false);
            set_ota_status(runtime, "Read failed", HAPANEL_SYSTEM_LEVEL_ERROR);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        if (read_size == 0) {
            ESP_LOGE(TAG,
                     "OTA HTTP stream ended early: written=%zu expected=%zu",
                     session.written_size,
                     session.expected_size);
            free(buffer);
            (void)hapanel_ota_abort(&session);
            set_progress_phase("error", false);
            set_ota_status(runtime, "Incomplete", HAPANEL_SYSTEM_LEVEL_ERROR);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_ERR_INVALID_SIZE;
        }

        result = hapanel_ota_write(&session, buffer, (size_t)read_size);
        if (result != ESP_OK) {
            free(buffer);
            (void)hapanel_ota_abort(&session);
            set_progress_phase("error", false);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return result;
        }
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    result = hapanel_ota_finish(&session);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "OTA HTTP image staged successfully; reboot is required");
    }
    return result;
}

static esp_err_t stage_running_image_for_self_test(hapanel_runtime_t *runtime, bool factory_only)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        set_ota_status(runtime, "Partition error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NOT_FOUND;
    }

    if (factory_only && running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        ESP_LOGW(TAG, "Self-test staging is only allowed from factory; running=%s", running->label);
        set_ota_status(runtime, "Factory only", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_ERR_NOT_SUPPORTED;
    }

    reset_progress();
    hapanel_ota_session_t session = {0};
    esp_err_t begin_result = hapanel_ota_begin(runtime, &session, running->size);
    if (begin_result != ESP_OK) {
        ESP_LOGW(TAG, "Self-test OTA begin failed: %s", esp_err_to_name(begin_result));
        return begin_result;
    }

    uint8_t *buffer = malloc(HAPANEL_OTA_COPY_CHUNK_SIZE);
    if (buffer == NULL) {
        hapanel_ota_abort(&session);
        set_ota_status(runtime, "No memory", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGW(TAG,
             "Starting local OTA self-test copy: source=%s target=%s size=%" PRIu32,
             running->label,
             session.target->label,
             running->size);

    for (uint32_t offset = 0; offset < running->size; offset += HAPANEL_OTA_COPY_CHUNK_SIZE) {
        const uint32_t remaining = running->size - offset;
        const size_t chunk_size = remaining < HAPANEL_OTA_COPY_CHUNK_SIZE
                                      ? remaining
                                      : HAPANEL_OTA_COPY_CHUNK_SIZE;

        esp_err_t read_result = esp_partition_read(running, offset, buffer, chunk_size);
        if (read_result != ESP_OK) {
            ESP_LOGE(TAG,
                     "Self-test OTA read failed at offset=%" PRIu32 ": %s",
                     offset,
                     esp_err_to_name(read_result));
            free(buffer);
            hapanel_ota_abort(&session);
            set_ota_status(runtime, "Read failed", HAPANEL_SYSTEM_LEVEL_ERROR);
            return read_result;
        }

        esp_err_t write_result = hapanel_ota_write(&session, buffer, chunk_size);
        if (write_result != ESP_OK) {
            free(buffer);
            hapanel_ota_abort(&session);
            return write_result;
        }
    }

    free(buffer);

    esp_err_t finish_result = hapanel_ota_finish(&session);
    if (finish_result != ESP_OK) {
        ESP_LOGE(TAG, "Self-test OTA finish failed: %s", esp_err_to_name(finish_result));
        return finish_result;
    }

    ESP_LOGW(TAG, "Local OTA self-test image staged; reboot is required to exercise rollback");
    return ESP_OK;
}

esp_err_t hapanel_ota_self_test_stage_running(hapanel_runtime_t *runtime)
{
    return stage_running_image_for_self_test(runtime, true);
}

esp_err_t hapanel_ota_self_test_stage_any_running(hapanel_runtime_t *runtime)
{
    return stage_running_image_for_self_test(runtime, false);
}

esp_err_t hapanel_ota_init(hapanel_runtime_t *runtime)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *ota_data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_DATA_OTA,
                                                               NULL);
    const esp_partition_t *boot = ota_data != NULL ? esp_ota_get_boot_partition() : running;
    const esp_partition_t *ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                            NULL);
    const esp_partition_t *ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                            ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                            NULL);

    ESP_LOGI(TAG, "Initializing OTA foundation");
    log_partition("running", running);
    log_partition("otadata", ota_data);
    log_partition("boot", boot);
    log_partition("ota_0", ota_0);
    log_partition("ota_1", ota_1);

    if (running == NULL || boot == NULL) {
        set_ota_status(runtime, "Partition error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NOT_FOUND;
    }

    if (running != boot) {
        ESP_LOGW(TAG, "Running partition differs from configured boot partition");
        set_ota_status(runtime, "Boot mismatch", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }

    const bool ota_slots_available = ota_0 != NULL && ota_1 != NULL;
    if (!ota_slots_available) {
        ESP_LOGW(TAG, "OTA update slots are not configured; firmware is factory-only for now");
        set_ota_status(runtime, "Factory only", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }

    hapanel_ota_preflight_t preflight;
    esp_err_t preflight_result = hapanel_ota_preflight(&preflight);
    if (preflight_result == ESP_OK) {
        ESP_LOGI(TAG, "Next OTA update target: %s", preflight.target_label);
    } else {
        ESP_LOGW(TAG, "OTA preflight is not ready: %s", preflight.reason);
    }

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_result = get_running_ota_state(running, &running_state);
    if (state_result == ESP_OK) {
        ESP_LOGI(TAG, "running OTA image state: %s", ota_state_name(running_state));
        if (running_state == ESP_OTA_IMG_PENDING_VERIFY) {
            set_ota_status(runtime, "Pending verify", HAPANEL_SYSTEM_LEVEL_PENDING);
            return ESP_OK;
        }
    } else if (state_result != ESP_ERR_NOT_SUPPORTED && state_result != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read running OTA image state: %s", esp_err_to_name(state_result));
        set_ota_status(runtime, "State unknown", HAPANEL_SYSTEM_LEVEL_WARNING);
        return ESP_OK;
    }
#else
    ESP_LOGW(TAG, "OTA rollback validation is disabled in sdkconfig");
    set_ota_status(runtime, "Rollback off", HAPANEL_SYSTEM_LEVEL_WARNING);
    return ESP_OK;
#endif

    set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
    return ESP_OK;
}

esp_err_t hapanel_ota_mark_boot_valid(hapanel_runtime_t *runtime)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        set_ota_status(runtime, "Partition error", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_ERR_NOT_FOUND;
    }

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ESP_LOGW(TAG, "Skipping boot validation mark; rollback support is disabled");
    set_ota_status(runtime, "Rollback off", HAPANEL_SYSTEM_LEVEL_WARNING);
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_result = get_running_ota_state(running, &running_state);
    if (state_result == ESP_ERR_NOT_SUPPORTED || state_result == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Running partition has no OTA state to validate");
        set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
        return ESP_OK;
    }

    if (state_result != ESP_OK) {
        ESP_LOGW(TAG, "Unable to read running OTA state for validation: %s",
                 esp_err_to_name(state_result));
        set_ota_status(runtime, "State unknown", HAPANEL_SYSTEM_LEVEL_WARNING);
        return state_result;
    }

    ESP_LOGI(TAG, "Boot validation check: running state=%s", ota_state_name(running_state));

    switch (running_state) {
    case ESP_OTA_IMG_PENDING_VERIFY: {
        esp_err_t mark_result = esp_ota_mark_app_valid_cancel_rollback();
        if (mark_result == ESP_OK) {
            ESP_LOGI(TAG, "Marked running OTA image valid");
            set_ota_status(runtime, "Validated", HAPANEL_SYSTEM_LEVEL_OK);
        } else {
            ESP_LOGE(TAG, "Failed to mark running OTA image valid: %s",
                     esp_err_to_name(mark_result));
            set_ota_status(runtime, "Validate failed", HAPANEL_SYSTEM_LEVEL_ERROR);
        }
        return mark_result;
    }
    case ESP_OTA_IMG_NEW:
        set_ota_status(runtime, "Awaiting verify", HAPANEL_SYSTEM_LEVEL_PENDING);
        return ESP_OK;
    case ESP_OTA_IMG_INVALID:
    case ESP_OTA_IMG_ABORTED:
        set_ota_status(runtime, "Invalid image", HAPANEL_SYSTEM_LEVEL_ERROR);
        return ESP_FAIL;
    case ESP_OTA_IMG_VALID:
    case ESP_OTA_IMG_UNDEFINED:
    default:
        set_ota_status(runtime, "Ready", HAPANEL_SYSTEM_LEVEL_OK);
        return ESP_OK;
    }
#endif
}
