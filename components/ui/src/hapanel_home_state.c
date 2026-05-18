#include "hapanel_home_state.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *label;
    const char *value;
} hapanel_home_entity_default_t;

static const hapanel_home_entity_default_t DEFAULT_ENTITIES[HAPANEL_HOME_ENTITY_COUNT] = {
    [HAPANEL_HOME_ENTITY_SCENE] = {
        .label = "Scene",
        .value = "Evening",
    },
    [HAPANEL_HOME_ENTITY_LIGHTS] = {
        .label = "Lights",
        .value = "Ready",
    },
    [HAPANEL_HOME_ENTITY_CLIMATE] = {
        .label = "Climate",
        .value = "Comfort",
    },
};

static void copy_text(char *target, size_t target_size, const char *value)
{
    if (target_size == 0) {
        return;
    }

    if (value == NULL || value[0] == '\0') {
        value = "Unknown";
    }

    snprintf(target, target_size, "%s", value);
}

void hapanel_home_state_init(hapanel_home_state_t *state)
{
    if (state == NULL) {
        return;
    }

    *state = (hapanel_home_state_t){0};
    state->revision = 1;
    for (size_t i = 0; i < HAPANEL_HOME_ENTITY_COUNT; ++i) {
        copy_text(state->entities[i].label,
                  sizeof(state->entities[i].label),
                  DEFAULT_ENTITIES[i].label);
        copy_text(state->entities[i].value,
                  sizeof(state->entities[i].value),
                  DEFAULT_ENTITIES[i].value);
        state->entities[i].online = false;
        state->entities[i].revision = 1;
    }
}

bool hapanel_home_state_update(hapanel_home_state_t *state,
                               hapanel_home_entity_id_t entity,
                               const char *value,
                               bool online)
{
    if (state == NULL || entity >= HAPANEL_HOME_ENTITY_COUNT) {
        return false;
    }

    hapanel_home_entity_t *item = &state->entities[entity];
    char next_value[HAPANEL_HOME_ENTITY_VALUE_MAX];
    copy_text(next_value, sizeof(next_value), value);

    if (item->online == online && strcmp(item->value, next_value) == 0) {
        return false;
    }

    copy_text(item->value, sizeof(item->value), next_value);
    item->online = online;
    item->revision++;
    state->revision++;
    return true;
}
