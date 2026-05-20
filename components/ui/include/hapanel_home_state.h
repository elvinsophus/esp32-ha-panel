#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAPANEL_HOME_ENTITY_COUNT 3
#define HAPANEL_HOME_ENTITY_LABEL_MAX 16
#define HAPANEL_HOME_ENTITY_VALUE_MAX 64
#define HAPANEL_HOME_DETAIL_ITEM_COUNT 3
#define HAPANEL_HOME_DETAIL_LABEL_MAX 18
#define HAPANEL_HOME_DETAIL_VALUE_MAX 48

typedef enum {
    HAPANEL_HOME_ENTITY_SCENE,
    HAPANEL_HOME_ENTITY_LIGHTS,
    HAPANEL_HOME_ENTITY_CLIMATE,
} hapanel_home_entity_id_t;

typedef struct {
    char label[HAPANEL_HOME_DETAIL_LABEL_MAX];
    char value[HAPANEL_HOME_DETAIL_VALUE_MAX];
    bool online;
    uint32_t revision;
} hapanel_home_detail_item_t;

typedef struct {
    char label[HAPANEL_HOME_ENTITY_LABEL_MAX];
    char value[HAPANEL_HOME_ENTITY_VALUE_MAX];
    hapanel_home_detail_item_t details[HAPANEL_HOME_DETAIL_ITEM_COUNT];
    size_t detail_count;
    bool online;
    uint32_t revision;
} hapanel_home_entity_t;

typedef struct {
    hapanel_home_entity_t entities[HAPANEL_HOME_ENTITY_COUNT];
    uint32_t revision;
} hapanel_home_state_t;

typedef struct {
    hapanel_home_entity_id_t entity;
    size_t detail_index;
    char category[HAPANEL_HOME_ENTITY_LABEL_MAX];
    char label[HAPANEL_HOME_DETAIL_LABEL_MAX];
    char value[HAPANEL_HOME_DETAIL_VALUE_MAX];
    bool online;
    uint32_t revision;
} hapanel_home_action_t;

void hapanel_home_state_init(hapanel_home_state_t *state);
bool hapanel_home_state_update(hapanel_home_state_t *state,
                               hapanel_home_entity_id_t entity,
                               const char *value,
                               bool online);
bool hapanel_home_state_update_payload(hapanel_home_state_t *state,
                                       hapanel_home_entity_id_t entity,
                                       const char *payload);
