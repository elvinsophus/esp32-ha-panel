#include "hapanel_system_status.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *label;
    const char *initial_value;
    hapanel_system_level_t initial_level;
} hapanel_system_status_default_t;

static const hapanel_system_status_default_t DEFAULT_STATUS[HAPANEL_SYSTEM_STATUS_COUNT] = {
    [HAPANEL_SYSTEM_PROFILE] = {
        .label = "Profile",
        .initial_value = "Resolving",
        .initial_level = HAPANEL_SYSTEM_LEVEL_PENDING,
    },
    [HAPANEL_SYSTEM_DISPLAY] = {
        .label = "Display",
        .initial_value = "Initializing",
        .initial_level = HAPANEL_SYSTEM_LEVEL_PENDING,
    },
    [HAPANEL_SYSTEM_TOUCH] = {
        .label = "Touch",
        .initial_value = "Initializing",
        .initial_level = HAPANEL_SYSTEM_LEVEL_PENDING,
    },
    [HAPANEL_SYSTEM_STORAGE] = {
        .label = "Storage",
        .initial_value = "Initializing",
        .initial_level = HAPANEL_SYSTEM_LEVEL_PENDING,
    },
    [HAPANEL_SYSTEM_WIFI] = {
        .label = "Wi-Fi",
        .initial_value = "Not configured",
        .initial_level = HAPANEL_SYSTEM_LEVEL_PENDING,
    },
    [HAPANEL_SYSTEM_MQTT] = {
        .label = "MQTT",
        .initial_value = "Offline",
        .initial_level = HAPANEL_SYSTEM_LEVEL_OFFLINE,
    },
    [HAPANEL_SYSTEM_OTA] = {
        .label = "OTA",
        .initial_value = "Pending",
        .initial_level = HAPANEL_SYSTEM_LEVEL_OFFLINE,
    },
};

static void copy_status_value(char *target, size_t target_size, const char *value)
{
    if (target_size == 0) {
        return;
    }

    if (value == NULL || value[0] == '\0') {
        value = "Unknown";
    }

    snprintf(target, target_size, "%s", value);
}

void hapanel_system_status_init(hapanel_system_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->item_count = HAPANEL_SYSTEM_STATUS_COUNT;
    status->psram_ready = false;
    status->revision = 1;

    for (size_t i = 0; i < HAPANEL_SYSTEM_STATUS_COUNT; ++i) {
        status->items[i].label = DEFAULT_STATUS[i].label;
        copy_status_value(status->items[i].value,
                          sizeof(status->items[i].value),
                          DEFAULT_STATUS[i].initial_value);
        status->items[i].level = DEFAULT_STATUS[i].initial_level;
    }
}

void hapanel_system_status_set_psram_ready(hapanel_system_status_t *status, bool ready)
{
    if (status == NULL) {
        return;
    }

    if (status->psram_ready == ready) {
        return;
    }

    status->psram_ready = ready;
    status->revision++;
}

void hapanel_system_status_set(hapanel_system_status_t *status,
                               hapanel_system_subsystem_t subsystem,
                               const char *value,
                               hapanel_system_level_t level)
{
    if (status == NULL || subsystem >= HAPANEL_SYSTEM_STATUS_COUNT) {
        return;
    }

    char next_value[HAPANEL_SYSTEM_STATUS_VALUE_MAX];
    copy_status_value(next_value, sizeof(next_value), value);

    if (strcmp(status->items[subsystem].value, next_value) == 0 &&
        status->items[subsystem].level == level) {
        return;
    }

    copy_status_value(status->items[subsystem].value,
                      sizeof(status->items[subsystem].value),
                      next_value);
    status->items[subsystem].level = level;
    status->revision++;
}
