#pragma once

#include "hapanel_home_state.h"
#include "hapanel_ui_status.h"

typedef enum {
    HAPANEL_UI_LAYER_AMBIENT,
    HAPANEL_UI_LAYER_ROOT,
    HAPANEL_UI_LAYER_DOMAIN,
    HAPANEL_UI_LAYER_DETAIL,
    HAPANEL_UI_LAYER_OVERLAY,
    HAPANEL_UI_LAYER_MODAL,
} hapanel_ui_layer_t;

typedef enum {
    HAPANEL_UI_PAGE_ROOT,
    HAPANEL_UI_PAGE_SYSTEM_STATUS,
    HAPANEL_UI_PAGE_HOME,
    HAPANEL_UI_PAGE_SECURITY,
    HAPANEL_UI_PAGE_APPS,
    HAPANEL_UI_PAGE_HOME_SCENE,
    HAPANEL_UI_PAGE_HOME_LIGHTS,
    HAPANEL_UI_PAGE_HOME_CLIMATE,
    HAPANEL_UI_PAGE_COUNT,
} hapanel_ui_page_id_t;

typedef struct {
    hapanel_ui_page_id_t id;
    hapanel_ui_layer_t layer;
    const char *name;
} hapanel_ui_page_descriptor_t;

typedef void (*hapanel_ui_page_request_callback_t)(hapanel_ui_page_id_t page, void *context);

const hapanel_ui_page_descriptor_t *hapanel_ui_page_descriptor(hapanel_ui_page_id_t page);
hapanel_ui_page_id_t hapanel_ui_current_page(void);
hapanel_ui_layer_t hapanel_ui_current_layer(void);
void hapanel_ui_set_page_request_callback(hapanel_ui_page_request_callback_t callback,
                                          void *context);
void hapanel_ui_show_page(hapanel_ui_page_id_t page, const hapanel_ui_status_t *status);
void hapanel_ui_set_home_state(const hapanel_home_state_t *state);
void hapanel_ui_refresh_current_page(const hapanel_ui_status_t *status);
