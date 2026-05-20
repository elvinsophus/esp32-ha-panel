#include "hapanel_runtime.h"

#include "hapanel_ui.h"

#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static hapanel_ui_status_level_t ui_level_from_system(hapanel_system_level_t level)
{
    switch (level) {
    case HAPANEL_SYSTEM_LEVEL_OK:
        return HAPANEL_UI_STATUS_OK;
    case HAPANEL_SYSTEM_LEVEL_PENDING:
        return HAPANEL_UI_STATUS_PENDING;
    case HAPANEL_SYSTEM_LEVEL_WARNING:
        return HAPANEL_UI_STATUS_WARNING;
    case HAPANEL_SYSTEM_LEVEL_ERROR:
        return HAPANEL_UI_STATUS_ERROR;
    case HAPANEL_SYSTEM_LEVEL_OFFLINE:
    default:
        return HAPANEL_UI_STATUS_OFFLINE;
    }
}

static void lock_runtime(hapanel_runtime_t *runtime)
{
    if (runtime != NULL && runtime->lock != NULL) {
        xSemaphoreTake(runtime->lock, portMAX_DELAY);
    }
}

static void unlock_runtime(hapanel_runtime_t *runtime)
{
    if (runtime != NULL && runtime->lock != NULL) {
        xSemaphoreGive(runtime->lock);
    }
}

static void sync_ui_snapshot(hapanel_runtime_t *runtime)
{
    const hapanel_system_status_t *system_status = &runtime->system_status;

    lock_runtime(runtime);
    for (size_t i = 0; i < system_status->item_count; ++i) {
        runtime->ui_items[i].label = system_status->items[i].label;
        snprintf(runtime->ui_item_values[i],
                 sizeof(runtime->ui_item_values[i]),
                 "%s",
                 system_status->items[i].value);
        runtime->ui_items[i].value = runtime->ui_item_values[i];
        runtime->ui_items[i].level = ui_level_from_system(system_status->items[i].level);
    }

    runtime->ui_status.items = runtime->ui_items;
    runtime->ui_status.item_count = system_status->item_count;
    runtime->ui_status.psram_ready = system_status->psram_ready;
    runtime->ui_status.uptime_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    runtime->ui_home_state = runtime->home_state;
    runtime->ui_system_revision = system_status->revision;
    runtime->ui_home_revision = runtime->home_state.revision;
    unlock_runtime(runtime);
}

static void notify_status_changed(hapanel_runtime_t *runtime)
{
    if (runtime->status_callback != NULL) {
        runtime->status_callback(runtime->status_context);
    }

    if (runtime->refresh_callback != NULL) {
        runtime->refresh_callback(runtime->refresh_context);
    }
}

void hapanel_runtime_init(hapanel_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    *runtime = (hapanel_runtime_t){0};
    runtime->lock = xSemaphoreCreateMutex();
    hapanel_system_status_init(&runtime->system_status);
    hapanel_home_state_init(&runtime->home_state);
    runtime->requested_page = HAPANEL_UI_PAGE_ROOT;
    runtime->rendered_page = HAPANEL_UI_PAGE_ROOT;
    sync_ui_snapshot(runtime);
}

void hapanel_runtime_set_psram_ready(hapanel_runtime_t *runtime, bool ready)
{
    if (runtime == NULL) {
        return;
    }

    lock_runtime(runtime);
    const uint32_t previous_revision = runtime->system_status.revision;
    hapanel_system_status_set_psram_ready(&runtime->system_status, ready);
    const bool changed = runtime->system_status.revision != previous_revision;
    unlock_runtime(runtime);
    if (!changed) {
        return;
    }

    notify_status_changed(runtime);
}

void hapanel_runtime_set_status(hapanel_runtime_t *runtime,
                                hapanel_system_subsystem_t subsystem,
                                const char *value,
                                hapanel_system_level_t level)
{
    if (runtime == NULL) {
        return;
    }

    lock_runtime(runtime);
    const uint32_t previous_revision = runtime->system_status.revision;
    hapanel_system_status_set(&runtime->system_status, subsystem, value, level);
    const bool changed = runtime->system_status.revision != previous_revision;
    unlock_runtime(runtime);
    if (!changed) {
        return;
    }

    notify_status_changed(runtime);
}

void hapanel_runtime_set_home_entity(hapanel_runtime_t *runtime,
                                     hapanel_home_entity_id_t entity,
                                     const char *value,
                                     bool online)
{
    if (runtime == NULL) {
        return;
    }

    lock_runtime(runtime);
    const bool changed = hapanel_home_state_update(&runtime->home_state, entity, value, online);
    unlock_runtime(runtime);
    if (!changed) {
        return;
    }

    notify_status_changed(runtime);
}

void hapanel_runtime_set_home_entity_payload(hapanel_runtime_t *runtime,
                                             hapanel_home_entity_id_t entity,
                                             const char *payload)
{
    if (runtime == NULL) {
        return;
    }

    lock_runtime(runtime);
    const bool changed = hapanel_home_state_update_payload(&runtime->home_state, entity, payload);
    unlock_runtime(runtime);
    if (!changed) {
        return;
    }

    notify_status_changed(runtime);
}

void hapanel_runtime_set_refresh_callback(hapanel_runtime_t *runtime,
                                          void (*callback)(void *context),
                                          void *context)
{
    if (runtime == NULL) {
        return;
    }

    runtime->refresh_callback = callback;
    runtime->refresh_context = context;
}

void hapanel_runtime_set_status_callback(hapanel_runtime_t *runtime,
                                         void (*callback)(void *context),
                                         void *context)
{
    if (runtime == NULL) {
        return;
    }

    runtime->status_callback = callback;
    runtime->status_context = context;
}

void hapanel_runtime_request_refresh(hapanel_runtime_t *runtime)
{
    if (runtime == NULL || runtime->refresh_callback == NULL) {
        return;
    }

    if (runtime->root_visible) {
        runtime->rendered_revision = 0;
    }

    runtime->refresh_callback(runtime->refresh_context);
}

void hapanel_runtime_show_page(hapanel_runtime_t *runtime, hapanel_ui_page_id_t page)
{
    if (runtime == NULL || page >= HAPANEL_UI_PAGE_COUNT) {
        return;
    }

    runtime->requested_page = page;
    runtime->rendered_revision = 0;

    if (runtime->refresh_callback != NULL) {
        runtime->refresh_callback(runtime->refresh_context);
    }
}

void hapanel_runtime_handle_ui_page_request(hapanel_runtime_t *runtime,
                                            hapanel_ui_page_id_t page)
{
    if (runtime == NULL || page >= HAPANEL_UI_PAGE_COUNT) {
        return;
    }

    hapanel_runtime_render_page(runtime, page);

    if (runtime->status_callback != NULL) {
        runtime->status_callback(runtime->status_context);
    }
}

void hapanel_runtime_render_page(hapanel_runtime_t *runtime, hapanel_ui_page_id_t page)
{
    if (runtime == NULL) {
        return;
    }

    sync_ui_snapshot(runtime);
    hapanel_ui_set_home_state(&runtime->ui_home_state);
    hapanel_ui_show_page(page, &runtime->ui_status);
    runtime->requested_page = page;
    runtime->rendered_page = page;
    runtime->rendered_revision = runtime->ui_system_revision;
    runtime->rendered_home_revision = runtime->ui_home_revision;
    runtime->root_visible = true;
}

void hapanel_runtime_refresh_current_page(hapanel_runtime_t *runtime)
{
    if (runtime == NULL || !runtime->root_visible) {
        return;
    }

    if (runtime->rendered_page != runtime->requested_page) {
        hapanel_runtime_render_page(runtime, runtime->requested_page);
        return;
    }

    lock_runtime(runtime);
    const uint32_t system_revision = runtime->system_status.revision;
    const uint32_t home_revision = runtime->home_state.revision;
    unlock_runtime(runtime);

    if (runtime->rendered_revision == system_revision &&
        runtime->rendered_home_revision == home_revision) {
        return;
    }

    sync_ui_snapshot(runtime);
    hapanel_ui_refresh_current_page(&runtime->ui_status);
    runtime->rendered_revision = runtime->ui_system_revision;
    runtime->rendered_home_revision = runtime->ui_home_revision;
}

void hapanel_runtime_render_root(hapanel_runtime_t *runtime)
{
    hapanel_runtime_render_page(runtime, HAPANEL_UI_PAGE_ROOT);
}

void hapanel_runtime_refresh_root(hapanel_runtime_t *runtime)
{
    hapanel_runtime_refresh_current_page(runtime);
}
