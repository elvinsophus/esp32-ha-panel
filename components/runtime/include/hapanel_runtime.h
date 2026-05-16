#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hapanel_system_status.h"
#include "hapanel_ui_status.h"

typedef struct {
    hapanel_system_status_t system_status;
    hapanel_ui_status_item_t ui_items[HAPANEL_SYSTEM_STATUS_COUNT];
    hapanel_ui_status_t ui_status;
    uint32_t rendered_revision;
    bool root_visible;
} hapanel_runtime_t;

void hapanel_runtime_init(hapanel_runtime_t *runtime);
void hapanel_runtime_set_psram_ready(hapanel_runtime_t *runtime, bool ready);
void hapanel_runtime_set_status(hapanel_runtime_t *runtime,
                                hapanel_system_subsystem_t subsystem,
                                const char *value,
                                hapanel_system_level_t level);
void hapanel_runtime_render_root(hapanel_runtime_t *runtime);
void hapanel_runtime_refresh_root(hapanel_runtime_t *runtime);
