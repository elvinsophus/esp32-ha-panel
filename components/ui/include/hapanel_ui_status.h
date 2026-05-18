#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAPANEL_UI_STATUS_MAX_ITEMS 8

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
    uint64_t uptime_ms;
} hapanel_ui_status_t;
