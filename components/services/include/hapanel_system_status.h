#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HAPANEL_SYSTEM_PROFILE,
    HAPANEL_SYSTEM_DISPLAY,
    HAPANEL_SYSTEM_TOUCH,
    HAPANEL_SYSTEM_STORAGE,
    HAPANEL_SYSTEM_WIFI,
    HAPANEL_SYSTEM_MQTT,
    HAPANEL_SYSTEM_OTA,
    HAPANEL_SYSTEM_STATUS_COUNT,
} hapanel_system_subsystem_t;

typedef enum {
    HAPANEL_SYSTEM_LEVEL_OK,
    HAPANEL_SYSTEM_LEVEL_PENDING,
    HAPANEL_SYSTEM_LEVEL_OFFLINE,
    HAPANEL_SYSTEM_LEVEL_WARNING,
    HAPANEL_SYSTEM_LEVEL_ERROR,
} hapanel_system_level_t;

typedef struct {
    const char *label;
    const char *value;
    hapanel_system_level_t level;
} hapanel_system_status_item_t;

typedef struct {
    hapanel_system_status_item_t items[HAPANEL_SYSTEM_STATUS_COUNT];
    size_t item_count;
    bool psram_ready;
    uint32_t revision;
} hapanel_system_status_t;

void hapanel_system_status_init(hapanel_system_status_t *status);
void hapanel_system_status_set_psram_ready(hapanel_system_status_t *status, bool ready);
void hapanel_system_status_set(hapanel_system_status_t *status,
                               hapanel_system_subsystem_t subsystem,
                               const char *value,
                               hapanel_system_level_t level);
