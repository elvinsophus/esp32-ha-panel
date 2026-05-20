#include "hapanel_home_state.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *label;
    const char *value;
    const char *detail_label;
    const char *detail_value;
} hapanel_home_entity_default_t;

static const hapanel_home_entity_default_t DEFAULT_ENTITIES[HAPANEL_HOME_ENTITY_COUNT] = {
    [HAPANEL_HOME_ENTITY_SCENE] = {
        .label = "Scene",
        .value = "Evening",
        .detail_label = "Active",
        .detail_value = "Evening",
    },
    [HAPANEL_HOME_ENTITY_LIGHTS] = {
        .label = "Lights",
        .value = "Ready",
        .detail_label = "Summary",
        .detail_value = "Ready",
    },
    [HAPANEL_HOME_ENTITY_CLIMATE] = {
        .label = "Climate",
        .value = "Comfort",
        .detail_label = "Reading",
        .detail_value = "Comfort",
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

static bool is_ascii_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static void copy_trimmed_text(char *target, size_t target_size, const char *start, size_t len)
{
    if (target == NULL || target_size == 0) {
        return;
    }

    if (start == NULL) {
        copy_text(target, target_size, NULL);
        return;
    }

    while (len > 0 && is_ascii_space(*start)) {
        ++start;
        --len;
    }

    while (len > 0 && is_ascii_space(start[len - 1])) {
        --len;
    }

    if (len == 0) {
        copy_text(target, target_size, NULL);
        return;
    }

    const size_t copy_len = len < target_size ? len : target_size - 1;
    memcpy(target, start, copy_len);
    target[copy_len] = '\0';
}

static bool value_is_online(const char *value)
{
    return value != NULL && value[0] != '\0' && strcmp(value, "unavailable") != 0 &&
           strcmp(value, "unknown") != 0 && strcmp(value, "offline") != 0;
}

static bool update_detail_item(hapanel_home_detail_item_t *item,
                               const char *label,
                               const char *value,
                               bool online)
{
    if (item == NULL) {
        return false;
    }

    char next_label[HAPANEL_HOME_DETAIL_LABEL_MAX];
    char next_value[HAPANEL_HOME_DETAIL_VALUE_MAX];
    copy_text(next_label, sizeof(next_label), label);
    copy_text(next_value, sizeof(next_value), value);

    if (item->online == online && strcmp(item->label, next_label) == 0 &&
        strcmp(item->value, next_value) == 0) {
        return false;
    }

    copy_text(item->label, sizeof(item->label), next_label);
    copy_text(item->value, sizeof(item->value), next_value);
    item->online = online;
    item->revision++;
    return true;
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
        state->entities[i].detail_count = 1;
        copy_text(state->entities[i].details[0].label,
                  sizeof(state->entities[i].details[0].label),
                  DEFAULT_ENTITIES[i].detail_label);
        copy_text(state->entities[i].details[0].value,
                  sizeof(state->entities[i].details[0].value),
                  DEFAULT_ENTITIES[i].detail_value);
        state->entities[i].details[0].online = false;
        state->entities[i].details[0].revision = 1;
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

bool hapanel_home_state_update_payload(hapanel_home_state_t *state,
                                       hapanel_home_entity_id_t entity,
                                       const char *payload)
{
    if (state == NULL || entity >= HAPANEL_HOME_ENTITY_COUNT) {
        return false;
    }

    hapanel_home_entity_t *item = &state->entities[entity];
    const char *cursor = payload != NULL ? payload : "";
    const char *line_end = strpbrk(cursor, "\r\n");
    const size_t summary_len = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);

    char summary[HAPANEL_HOME_ENTITY_VALUE_MAX];
    copy_trimmed_text(summary, sizeof(summary), cursor, summary_len);
    const bool online = value_is_online(summary);
    bool changed = hapanel_home_state_update(state, entity, summary, online);

    if (line_end == NULL) {
        const bool detail_changed = update_detail_item(&item->details[0], "Current", summary, online);
        if (detail_changed) {
            item->revision++;
            state->revision++;
            changed = true;
        }
        if (item->detail_count != 1) {
            item->detail_count = 1;
            item->revision++;
            state->revision++;
            changed = true;
        }
        return changed;
    }

    cursor = line_end;
    while (*cursor == '\r' || *cursor == '\n') {
        ++cursor;
    }

    size_t parsed_count = 0;
    while (*cursor != '\0' && parsed_count < HAPANEL_HOME_DETAIL_ITEM_COUNT) {
        line_end = strpbrk(cursor, "\r\n");
        const size_t line_len = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);

        const char *separator = memchr(cursor, ':', line_len);
        if (separator == NULL) {
            separator = memchr(cursor, '=', line_len);
        }

        char detail_label[HAPANEL_HOME_DETAIL_LABEL_MAX];
        char detail_value[HAPANEL_HOME_DETAIL_VALUE_MAX];
        if (separator != NULL) {
            copy_trimmed_text(detail_label, sizeof(detail_label), cursor, (size_t)(separator - cursor));
            copy_trimmed_text(detail_value,
                              sizeof(detail_value),
                              separator + 1,
                              line_len - (size_t)(separator - cursor) - 1);
        } else {
            char fallback_label[HAPANEL_HOME_DETAIL_LABEL_MAX];
            snprintf(fallback_label, sizeof(fallback_label), "Detail %u", (unsigned)(parsed_count + 1));
            copy_text(detail_label, sizeof(detail_label), fallback_label);
            copy_trimmed_text(detail_value, sizeof(detail_value), cursor, line_len);
        }

        if (strcmp(detail_label, "Unknown") != 0 || strcmp(detail_value, "Unknown") != 0) {
            const bool detail_changed =
                update_detail_item(&item->details[parsed_count],
                                   detail_label,
                                   detail_value,
                                   online && value_is_online(detail_value));
            if (detail_changed) {
                item->revision++;
                state->revision++;
                changed = true;
            }
            ++parsed_count;
        }

        if (line_end == NULL) {
            break;
        }
        cursor = line_end;
        while (*cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }
    }

    if (parsed_count == 0) {
        parsed_count = 1;
        const bool detail_changed = update_detail_item(&item->details[0], "Current", summary, online);
        if (detail_changed) {
            item->revision++;
            state->revision++;
            changed = true;
        }
    }

    if (item->detail_count != parsed_count) {
        item->detail_count = parsed_count;
        item->revision++;
        state->revision++;
        changed = true;
    }

    return changed;
}
