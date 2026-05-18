#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "hapanel_home_state.h"
#include "hapanel_system_status.h"
#include "hapanel_ui_layer.h"
#include "hapanel_ui_status.h"

typedef struct {
    hapanel_system_status_t system_status;
    hapanel_home_state_t home_state;
    SemaphoreHandle_t lock;
    hapanel_ui_status_item_t ui_items[HAPANEL_SYSTEM_STATUS_COUNT];
    char ui_item_values[HAPANEL_SYSTEM_STATUS_COUNT][HAPANEL_SYSTEM_STATUS_VALUE_MAX];
    hapanel_ui_status_t ui_status;
    hapanel_home_state_t ui_home_state;
    uint32_t ui_system_revision;
    uint32_t ui_home_revision;
    uint32_t rendered_revision;
    uint32_t rendered_home_revision;
    hapanel_ui_page_id_t requested_page;
    hapanel_ui_page_id_t rendered_page;
    bool root_visible;
    void (*refresh_callback)(void *context);
    void *refresh_context;
    void (*status_callback)(void *context);
    void *status_context;
} hapanel_runtime_t;

void hapanel_runtime_init(hapanel_runtime_t *runtime);
void hapanel_runtime_set_refresh_callback(hapanel_runtime_t *runtime,
                                          void (*callback)(void *context),
                                          void *context);
void hapanel_runtime_set_status_callback(hapanel_runtime_t *runtime,
                                         void (*callback)(void *context),
                                         void *context);
void hapanel_runtime_set_psram_ready(hapanel_runtime_t *runtime, bool ready);
void hapanel_runtime_set_status(hapanel_runtime_t *runtime,
                                hapanel_system_subsystem_t subsystem,
                                const char *value,
                                hapanel_system_level_t level);
void hapanel_runtime_set_home_entity(hapanel_runtime_t *runtime,
                                     hapanel_home_entity_id_t entity,
                                     const char *value,
                                     bool online);
void hapanel_runtime_request_refresh(hapanel_runtime_t *runtime);
void hapanel_runtime_show_page(hapanel_runtime_t *runtime, hapanel_ui_page_id_t page);
void hapanel_runtime_handle_ui_page_request(hapanel_runtime_t *runtime,
                                            hapanel_ui_page_id_t page);
void hapanel_runtime_render_page(hapanel_runtime_t *runtime, hapanel_ui_page_id_t page);
void hapanel_runtime_refresh_current_page(hapanel_runtime_t *runtime);
void hapanel_runtime_render_root(hapanel_runtime_t *runtime);
void hapanel_runtime_refresh_root(hapanel_runtime_t *runtime);
