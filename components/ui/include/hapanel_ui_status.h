#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HAPANEL_UI_STATUS_OK,
    HAPANEL_UI_STATUS_PENDING,
    HAPANEL_UI_STATUS_OFFLINE,
    HAPANEL_UI_STATUS_WARNING,
    HAPANEL_UI_STATUS_ERROR,
} hapanel_ui_status_level_t;

typedef struct {
    const char *label;
    const char *value;
    hapanel_ui_status_level_t level;
} hapanel_ui_status_item_t;

typedef struct {
    const hapanel_ui_status_item_t *items;
    size_t item_count;
    bool psram_ready;
} hapanel_ui_status_t;
