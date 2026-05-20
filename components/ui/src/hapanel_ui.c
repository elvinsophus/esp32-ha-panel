#include "hapanel_ui.h"

#include "hapanel_profile.h"
#include "hapanel_ui_fonts.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    HAPANEL_UI_DYNAMIC_TEXT_MAX = 128,
};

typedef struct {
    bool created;
    lv_obj_t *psram_label;
    lv_obj_t *clock_label;
    lv_obj_t *uptime_label;
} hapanel_ambient_view_t;

typedef struct {
    bool created;
    size_t row_count;
    lv_obj_t *psram_label;
    lv_obj_t *wifi_label;
    lv_obj_t *status_dots[HAPANEL_UI_STATUS_MAX_ITEMS];
    lv_obj_t *status_values[HAPANEL_UI_STATUS_MAX_ITEMS];
} hapanel_root_view_t;

typedef struct {
    bool created;
    lv_obj_t *psram_label;
    lv_obj_t *entity_values[HAPANEL_HOME_ENTITY_COUNT];
    lv_obj_t *entity_dots[HAPANEL_HOME_ENTITY_COUNT];
} hapanel_home_view_t;

typedef struct {
    bool created;
    hapanel_home_entity_id_t entity;
    lv_obj_t *psram_label;
    lv_obj_t *value_label;
    lv_obj_t *accent_dot;
    lv_obj_t *detail_dots[HAPANEL_HOME_DETAIL_ITEM_COUNT];
    lv_obj_t *detail_labels[HAPANEL_HOME_DETAIL_ITEM_COUNT];
    lv_obj_t *detail_values[HAPANEL_HOME_DETAIL_ITEM_COUNT];
    size_t detail_count;
} hapanel_home_detail_view_t;

typedef struct {
    bool created;
    lv_obj_t *psram_label;
} hapanel_static_page_view_t;

static hapanel_ambient_view_t ambient_view;
static hapanel_root_view_t root_view;
static hapanel_home_view_t home_view;
static hapanel_home_detail_view_t home_detail_view;
static hapanel_static_page_view_t static_page_view;
static const hapanel_home_state_t *home_state;
static hapanel_ui_page_request_callback_t page_request_callback;
static void *page_request_context;
static hapanel_ui_home_action_callback_t home_action_callback;
static void *home_action_context;

static void show_root_page(const hapanel_ui_status_t *status);
static void refresh_root_page(const hapanel_ui_status_t *status);
static void show_system_status_page(const hapanel_ui_status_t *status);
static void refresh_system_status_page(const hapanel_ui_status_t *status);
static void show_home_page(const hapanel_ui_status_t *status);
static void refresh_home_page(const hapanel_ui_status_t *status);
static void show_home_detail_page(const hapanel_ui_status_t *status,
                                  hapanel_home_entity_id_t entity_id);
static void refresh_home_detail_page(const hapanel_ui_status_t *status);
static void show_security_page(const hapanel_ui_status_t *status);
static void show_apps_page(const hapanel_ui_status_t *status);
static void refresh_static_page(const hapanel_ui_status_t *status);

static const hapanel_ui_page_descriptor_t page_descriptors[HAPANEL_UI_PAGE_COUNT] = {
    [HAPANEL_UI_PAGE_ROOT] = {
        .id = HAPANEL_UI_PAGE_ROOT,
        .layer = HAPANEL_UI_LAYER_ROOT,
        .name = "Root",
    },
    [HAPANEL_UI_PAGE_SYSTEM_STATUS] = {
        .id = HAPANEL_UI_PAGE_SYSTEM_STATUS,
        .layer = HAPANEL_UI_LAYER_ROOT,
        .name = "System Status",
    },
    [HAPANEL_UI_PAGE_HOME] = {
        .id = HAPANEL_UI_PAGE_HOME,
        .layer = HAPANEL_UI_LAYER_DOMAIN,
        .name = "Home Status",
    },
    [HAPANEL_UI_PAGE_SECURITY] = {
        .id = HAPANEL_UI_PAGE_SECURITY,
        .layer = HAPANEL_UI_LAYER_DOMAIN,
        .name = "Security",
    },
    [HAPANEL_UI_PAGE_APPS] = {
        .id = HAPANEL_UI_PAGE_APPS,
        .layer = HAPANEL_UI_LAYER_DOMAIN,
        .name = "Apps",
    },
    [HAPANEL_UI_PAGE_HOME_SCENE] = {
        .id = HAPANEL_UI_PAGE_HOME_SCENE,
        .layer = HAPANEL_UI_LAYER_DETAIL,
        .name = "Scene Detail",
    },
    [HAPANEL_UI_PAGE_HOME_LIGHTS] = {
        .id = HAPANEL_UI_PAGE_HOME_LIGHTS,
        .layer = HAPANEL_UI_LAYER_DETAIL,
        .name = "Lights Detail",
    },
    [HAPANEL_UI_PAGE_HOME_CLIMATE] = {
        .id = HAPANEL_UI_PAGE_HOME_CLIMATE,
        .layer = HAPANEL_UI_LAYER_DETAIL,
        .name = "Climate Detail",
    },
};

static hapanel_ui_page_id_t current_page = HAPANEL_UI_PAGE_ROOT;
static hapanel_ui_layer_t current_layer = HAPANEL_UI_LAYER_ROOT;

static const hapanel_profile_t *ui_profile(void)
{
    return hapanel_profile_active();
}

static void reset_views(void)
{
    ambient_view = (hapanel_ambient_view_t){0};
    root_view = (hapanel_root_view_t){0};
    home_view = (hapanel_home_view_t){0};
    home_detail_view = (hapanel_home_detail_view_t){0};
    static_page_view = (hapanel_static_page_view_t){0};
}

static lv_color_t color_for_status(hapanel_ui_status_level_t level)
{
    const hapanel_theme_profile_t *theme = &ui_profile()->theme;

    switch (level) {
    case HAPANEL_UI_STATUS_OK:
        return lv_color_hex(theme->status_ok);
    case HAPANEL_UI_STATUS_PENDING:
        return lv_color_hex(theme->status_pending);
    case HAPANEL_UI_STATUS_WARNING:
        return lv_color_hex(theme->status_warning);
    case HAPANEL_UI_STATUS_ERROR:
        return lv_color_hex(theme->status_error);
    case HAPANEL_UI_STATUS_OFFLINE:
    default:
        return lv_color_hex(theme->status_offline);
    }
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                              lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static lv_obj_t *create_dynamic_label(lv_obj_t *parent, const char *surface, const char *text,
                                      lv_color_t color)
{
    char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
    const lv_font_t *font =
        hapanel_ui_font_prepare_dynamic_text(text, display_text, sizeof(display_text));
    hapanel_ui_font_log_missing_glyphs(surface, text, font);
    return create_label(parent, display_text, font, color);
}

static void configure_column(lv_obj_t *obj, int32_t row_gap)
{
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(obj, row_gap, 0);
}

static void create_status_bar(lv_obj_t *parent,
                              const hapanel_ui_status_t *status,
                              lv_obj_t **psram_label,
                              lv_obj_t **wifi_label)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, profile->layout.status_bar_height);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    create_label(bar, "HAPanel", hapanel_ui_font_static_16(),
                 lv_color_hex(profile->theme.text_primary));

    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, LV_SIZE_CONTENT);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, profile->spacing.sm, 0);

    lv_obj_t *psram = create_label(right,
                                   status->psram_ready ? "PSRAM" : "RAM",
                                   hapanel_ui_font_static_12(),
                                   status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                       : lv_color_hex(profile->theme.status_warning));
    lv_obj_t *wifi = create_label(right, "Wi-Fi", hapanel_ui_font_static_12(),
                                  lv_color_hex(profile->theme.text_muted));
    if (psram_label != NULL) {
        *psram_label = psram;
    }
    if (wifi_label != NULL) {
        *wifi_label = wifi;
    }
}

static void create_status_row(lv_obj_t *parent, const hapanel_ui_status_item_t *item, size_t index)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 30);
    lv_obj_set_style_pad_hor(row, 0, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_height(left, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, 0);

    lv_obj_t *dot = lv_obj_create(left);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color_for_status(item->level), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    create_label(left, item->label, hapanel_ui_font_static_16(), lv_color_hex(0xb7c1cd));
    lv_obj_t *value = create_dynamic_label(row, item->label, item->value,
                                           color_for_status(item->level));

    if (index < HAPANEL_UI_STATUS_MAX_ITEMS) {
        root_view.status_dots[index] = dot;
        root_view.status_values[index] = value;
    }
}

static lv_obj_t *create_panel(lv_obj_t *parent)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel, profile->spacing.sm, 0);
    lv_obj_set_style_radius(panel, profile->radius.sm, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(profile->theme.surface), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(profile->theme.surface_border), 0);
    configure_column(panel, 4);
    return panel;
}

static lv_obj_t *create_pill_label(lv_obj_t *parent, const char *text, lv_color_t color)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_height(pill, 32);
    lv_obj_set_width(pill, 118);
    lv_obj_set_style_pad_hor(pill, 14, 0);
    lv_obj_set_style_radius(pill, 16, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(profile->theme.surface), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(profile->theme.surface_border), 0);
    lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    create_label(pill, text, hapanel_ui_font_static_12(), color);
    return pill;
}

static void request_page_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || page_request_callback == NULL) {
        return;
    }

    const uintptr_t page_value = (uintptr_t)lv_event_get_user_data(event);
    if (page_value >= HAPANEL_UI_PAGE_COUNT) {
        return;
    }

    page_request_callback((hapanel_ui_page_id_t)page_value, page_request_context);
}

static void request_page(hapanel_ui_page_id_t page)
{
    if (page_request_callback == NULL || page >= HAPANEL_UI_PAGE_COUNT || page == current_page) {
        return;
    }

    page_request_callback(page, page_request_context);
}

static hapanel_ui_page_id_t home_detail_page_for_entity(hapanel_home_entity_id_t entity_id)
{
    switch (entity_id) {
    case HAPANEL_HOME_ENTITY_SCENE:
        return HAPANEL_UI_PAGE_HOME_SCENE;
    case HAPANEL_HOME_ENTITY_LIGHTS:
        return HAPANEL_UI_PAGE_HOME_LIGHTS;
    case HAPANEL_HOME_ENTITY_CLIMATE:
    default:
        return HAPANEL_UI_PAGE_HOME_CLIMATE;
    }
}

static hapanel_home_entity_id_t entity_for_home_detail_page(hapanel_ui_page_id_t page)
{
    switch (page) {
    case HAPANEL_UI_PAGE_HOME_SCENE:
        return HAPANEL_HOME_ENTITY_SCENE;
    case HAPANEL_UI_PAGE_HOME_LIGHTS:
        return HAPANEL_HOME_ENTITY_LIGHTS;
    case HAPANEL_UI_PAGE_HOME_CLIMATE:
    default:
        return HAPANEL_HOME_ENTITY_CLIMATE;
    }
}

static void make_page_target(lv_obj_t *obj, hapanel_ui_page_id_t page)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,
                        request_page_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)page);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, LV_STATE_PRESSED);
}

static void request_home_action_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || home_action_callback == NULL) {
        return;
    }

    const uintptr_t detail_index = (uintptr_t)lv_event_get_user_data(event);
    if (detail_index >= HAPANEL_HOME_DETAIL_ITEM_COUNT) {
        return;
    }

    home_action_callback(home_detail_view.entity, (size_t)detail_index, home_action_context);
}

static void make_home_action_target(lv_obj_t *obj, size_t detail_index)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,
                        request_home_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)detail_index);
    lv_obj_set_style_radius(obj, profile->radius.sm, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(profile->theme.surface_border), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_30, LV_STATE_PRESSED);
}

static void gesture_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    switch (current_page) {
    case HAPANEL_UI_PAGE_ROOT:
        if (dir == LV_DIR_RIGHT) {
            request_page(HAPANEL_UI_PAGE_HOME);
        } else if (dir == LV_DIR_TOP) {
            request_page(HAPANEL_UI_PAGE_SECURITY);
        } else if (dir == LV_DIR_LEFT) {
            request_page(HAPANEL_UI_PAGE_APPS);
        }
        break;
    case HAPANEL_UI_PAGE_HOME:
        if (dir == LV_DIR_LEFT) {
            request_page(HAPANEL_UI_PAGE_ROOT);
        }
        break;
    case HAPANEL_UI_PAGE_HOME_SCENE:
    case HAPANEL_UI_PAGE_HOME_LIGHTS:
    case HAPANEL_UI_PAGE_HOME_CLIMATE:
        if (dir == LV_DIR_RIGHT) {
            request_page(HAPANEL_UI_PAGE_HOME);
        }
        break;
    case HAPANEL_UI_PAGE_SECURITY:
        if (dir == LV_DIR_BOTTOM) {
            request_page(HAPANEL_UI_PAGE_ROOT);
        }
        break;
    case HAPANEL_UI_PAGE_APPS:
        if (dir == LV_DIR_RIGHT) {
            request_page(HAPANEL_UI_PAGE_ROOT);
        }
        break;
    case HAPANEL_UI_PAGE_SYSTEM_STATUS:
    default:
        break;
    }
}

static void add_gesture_bubble_recursive(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; ++i) {
        add_gesture_bubble_recursive(lv_obj_get_child(obj, i));
    }
}

static void enable_page_gestures(lv_obj_t *root)
{
    if (root == NULL) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_event_cb(screen, gesture_event_cb);
    lv_obj_add_event_cb(screen, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(root, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    add_gesture_bubble_recursive(root);
}

static void configure_screen_root(lv_obj_t *root)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(root, profile->spacing.sm, 0);
    lv_obj_set_style_pad_bottom(root, profile->spacing.sm, 0);
    lv_obj_set_style_pad_top(root, 0, 0);
    configure_column(root, profile->spacing.sm);
}

static void format_uptime_clock(uint64_t uptime_ms, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    const uint64_t total_minutes = uptime_ms / 60000ULL;
    const uint64_t hours = (total_minutes / 60ULL) % 100ULL;
    const uint64_t minutes = total_minutes % 60ULL;
    snprintf(buffer, buffer_size, "%02llu:%02llu", hours, minutes);
}

static void format_uptime_caption(uint64_t uptime_ms, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    const uint64_t total_seconds = uptime_ms / 1000ULL;
    const uint64_t hours = total_seconds / 3600ULL;
    const uint64_t minutes = (total_seconds / 60ULL) % 60ULL;
    snprintf(buffer, buffer_size, "Online %lluh %02llum", hours, minutes);
}

static lv_obj_t *create_home_tile(lv_obj_t *parent,
                                  const char *label,
                                  const char *value,
                                  lv_color_t accent,
                                  lv_obj_t **accent_dot,
                                  lv_obj_t **value_label)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 204, 126);
    lv_obj_set_style_pad_all(tile, 14, 0);
    lv_obj_set_style_radius(tile, profile->radius.sm, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(profile->theme.surface), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(profile->theme.surface_border), 0);
    configure_column(tile, 7);

    lv_obj_t *dot = lv_obj_create(tile);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    if (accent_dot != NULL) {
        *accent_dot = dot;
    }

    create_label(tile, label, hapanel_ui_font_static_12(), lv_color_hex(profile->theme.text_muted));
    lv_obj_t *value_obj = create_dynamic_label(tile, label, value,
                                               lv_color_hex(profile->theme.text_primary));
    lv_label_set_long_mode(value_obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(value_obj, LV_PCT(100));
    lv_obj_set_height(value_obj, 58);
    lv_obj_set_style_text_line_space(value_obj, 2, 0);

    if (value_label != NULL) {
        *value_label = value_obj;
    }

    return tile;
}

static void create_home_detail_row(lv_obj_t *parent,
                                   const hapanel_home_detail_item_t *item,
                                   size_t index,
                                   lv_color_t accent)
{
    const hapanel_profile_t *profile = ui_profile();

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_height(left, LV_SIZE_CONTENT);
    lv_obj_set_width(left, 132);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, 0);

    lv_obj_t *dot = lv_obj_create(left);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot,
                              item != NULL && item->online
                                  ? accent
                                  : lv_color_hex(profile->theme.status_offline),
                              0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    lv_obj_t *label = create_dynamic_label(left,
                                           "Home detail label",
                                           item != NULL ? item->label : "Detail",
                                           lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 104);

    lv_obj_t *value = create_dynamic_label(row,
                                           item != NULL ? item->label : "Detail",
                                           item != NULL ? item->value : "Unknown",
                                           item != NULL && item->online
                                               ? lv_color_hex(profile->theme.text_primary)
                                               : lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value, 230);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    make_home_action_target(row, index);
    make_home_action_target(left, index);
    make_home_action_target(dot, index);
    make_home_action_target(label, index);
    make_home_action_target(value, index);

    if (index < HAPANEL_HOME_DETAIL_ITEM_COUNT) {
        home_detail_view.detail_dots[index] = dot;
        home_detail_view.detail_labels[index] = label;
        home_detail_view.detail_values[index] = value;
    }
}

static const hapanel_home_entity_t *home_entity_or_null(hapanel_home_entity_id_t entity_id)
{
    if (home_state == NULL || entity_id >= HAPANEL_HOME_ENTITY_COUNT) {
        return NULL;
    }

    return &home_state->entities[entity_id];
}

static void show_root_page(const hapanel_ui_status_t *status)
{
    const hapanel_profile_t *profile = ui_profile();

    ambient_view = (hapanel_ambient_view_t){0};
    root_view = (hapanel_root_view_t){0};
    home_view = (hapanel_home_view_t){0};

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(profile->theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    configure_screen_root(root);

    create_status_bar(root, status, &ambient_view.psram_label, NULL);

    lv_obj_t *hero = lv_obj_create(root);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 178);
    lv_obj_set_layout(hero, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 6, 0);

    char clock_text[8];
    char uptime_text[24];
    format_uptime_clock(status->uptime_ms, clock_text, sizeof(clock_text));
    format_uptime_caption(status->uptime_ms, uptime_text, sizeof(uptime_text));

    ambient_view.clock_label = create_label(hero,
                                            clock_text,
                                            hapanel_ui_font_static_26(),
                                            lv_color_hex(profile->theme.text_primary));
    lv_obj_set_style_transform_zoom(ambient_view.clock_label, 300, 0);

    ambient_view.uptime_label = create_label(hero,
                                             uptime_text,
                                             hapanel_ui_font_static_16(),
                                             lv_color_hex(profile->theme.text_muted));

    lv_obj_t *assistant = lv_obj_create(root);
    lv_obj_remove_style_all(assistant);
    lv_obj_set_width(assistant, LV_PCT(100));
    lv_obj_set_height(assistant, 124);
    lv_obj_set_style_pad_all(assistant, profile->spacing.sm, 0);
    lv_obj_set_style_radius(assistant, profile->radius.md, 0);
    lv_obj_set_style_bg_color(assistant, lv_color_hex(profile->theme.surface), 0);
    lv_obj_set_style_bg_opa(assistant, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(assistant, 1, 0);
    lv_obj_set_style_border_color(assistant, lv_color_hex(profile->theme.surface_border), 0);
    lv_obj_set_layout(assistant, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(assistant, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(assistant, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(assistant, 10, 0);

    lv_obj_t *eyes = lv_obj_create(assistant);
    lv_obj_remove_style_all(eyes);
    lv_obj_set_size(eyes, 128, 36);
    lv_obj_set_layout(eyes, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(eyes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eyes, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    for (size_t i = 0; i < 2; ++i) {
        lv_obj_t *eye = lv_obj_create(eyes);
        lv_obj_remove_style_all(eye);
        lv_obj_set_size(eye, 42, 18);
        lv_obj_set_style_radius(eye, 9, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(profile->theme.text_primary), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_70, 0);
    }

    create_label(assistant, "Assistant idle", hapanel_ui_font_static_12(),
                 lv_color_hex(profile->theme.text_muted));

    lv_obj_t *hints = lv_obj_create(root);
    lv_obj_remove_style_all(hints);
    lv_obj_set_width(hints, LV_PCT(100));
    lv_obj_set_height(hints, 50);
    lv_obj_set_layout(hints, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hints, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hints, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *home_hint = create_pill_label(hints, "< Home", lv_color_hex(profile->theme.text_muted));
    make_page_target(home_hint, HAPANEL_UI_PAGE_HOME);
    lv_obj_t *security_hint =
        create_pill_label(hints, "Security", lv_color_hex(profile->theme.text_muted));
    make_page_target(security_hint, HAPANEL_UI_PAGE_SECURITY);
    lv_obj_t *apps_hint = create_pill_label(hints, "Apps >", lv_color_hex(profile->theme.text_muted));
    make_page_target(apps_hint, HAPANEL_UI_PAGE_APPS);

    ambient_view.created = true;
    enable_page_gestures(root);
    refresh_root_page(status);
}

static void show_system_status_page(const hapanel_ui_status_t *status)
{
    const hapanel_profile_t *profile = ui_profile();

    reset_views();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(profile->theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    configure_screen_root(root);

    create_status_bar(root, status, &root_view.psram_label, &root_view.wifi_label);

    lv_obj_t *hero = lv_obj_create(root);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 72);
    lv_obj_set_layout(hero, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 4, 0);

    lv_obj_t *title = lv_obj_create(hero);
    lv_obj_remove_style_all(title);
    lv_obj_set_height(title, LV_SIZE_CONTENT);
    configure_column(title, 4);
    create_label(title, "HAPanel", hapanel_ui_font_static_26(),
                 lv_color_hex(profile->theme.text_primary));
    create_label(title, profile->board.name, hapanel_ui_font_static_16(),
                 lv_color_hex(profile->theme.text_muted));
    lv_obj_t *root_target =
        create_pill_label(hero, "Root", lv_color_hex(profile->theme.text_muted));
    make_page_target(root_target, HAPANEL_UI_PAGE_ROOT);

    lv_obj_t *panel = create_panel(root);
    create_label(panel, "System Status", hapanel_ui_font_static_18(),
                 lv_color_hex(profile->theme.text_primary));

    const size_t row_count = status->item_count < HAPANEL_UI_STATUS_MAX_ITEMS
                                 ? status->item_count
                                 : HAPANEL_UI_STATUS_MAX_ITEMS;
    root_view.row_count = row_count;

    for (size_t i = 0; i < row_count; ++i) {
        create_status_row(panel, &status->items[i], i);
    }

    lv_obj_t *footer = create_label(root, "Core services will appear here as they come online.",
                                    hapanel_ui_font_static_12(),
                                    lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(footer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(footer, LV_PCT(100));

    root_view.created = true;
    enable_page_gestures(root);
    refresh_system_status_page(status);
}

static void show_home_page(const hapanel_ui_status_t *status)
{
    const hapanel_profile_t *profile = ui_profile();

    reset_views();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(profile->theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    configure_screen_root(root);

    create_status_bar(root, status, &home_view.psram_label, NULL);

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 54);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    create_label(header, "Home Status", hapanel_ui_font_static_26(),
                 lv_color_hex(profile->theme.text_primary));
    lv_obj_t *root_target =
        create_pill_label(header, "Root", lv_color_hex(profile->theme.text_muted));
    make_page_target(root_target, HAPANEL_UI_PAGE_ROOT);

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, 292);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, profile->spacing.sm, 0);
    lv_obj_set_style_pad_column(grid, profile->spacing.sm, 0);

    const uint32_t entity_accents[HAPANEL_HOME_ENTITY_COUNT] = {
        [HAPANEL_HOME_ENTITY_SCENE] = 0x7ec8e3,
        [HAPANEL_HOME_ENTITY_LIGHTS] = 0xf0c674,
        [HAPANEL_HOME_ENTITY_CLIMATE] = 0x9ccfd8,
    };
    for (size_t i = 0; i < HAPANEL_HOME_ENTITY_COUNT; ++i) {
        const hapanel_home_entity_t *entity =
            home_state != NULL ? &home_state->entities[i] : NULL;
        lv_obj_t *tile =
            create_home_tile(grid,
                             entity != NULL ? entity->label : "Entity",
                             entity != NULL ? entity->value : "Unknown",
                             entity != NULL && entity->online
                                 ? lv_color_hex(entity_accents[i])
                                 : lv_color_hex(profile->theme.status_offline),
                             &home_view.entity_dots[i],
                             &home_view.entity_values[i]);
        make_page_target(tile, home_detail_page_for_entity((hapanel_home_entity_id_t)i));
    }
    lv_obj_t *footer =
        create_label(root,
                     "Pinned home categories update from retained Home Assistant topics.",
                     hapanel_ui_font_static_12(),
                     lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(footer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(footer, LV_PCT(100));

    home_view.created = true;
    enable_page_gestures(root);
    refresh_home_page(status);
}

static void show_home_detail_page(const hapanel_ui_status_t *status,
                                  hapanel_home_entity_id_t entity_id)
{
    const hapanel_profile_t *profile = ui_profile();
    const hapanel_home_entity_t *entity = home_entity_or_null(entity_id);
    const char *label = entity != NULL ? entity->label : "Home";
    const char *value = entity != NULL ? entity->value : "Unknown";
    const bool online = entity != NULL && entity->online;

    const uint32_t entity_accents[HAPANEL_HOME_ENTITY_COUNT] = {
        [HAPANEL_HOME_ENTITY_SCENE] = 0x7ec8e3,
        [HAPANEL_HOME_ENTITY_LIGHTS] = 0xf0c674,
        [HAPANEL_HOME_ENTITY_CLIMATE] = 0x9ccfd8,
    };

    reset_views();
    home_detail_view.entity = entity_id;

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(profile->theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    configure_screen_root(root);

    create_status_bar(root, status, &home_detail_view.psram_label, NULL);

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 64);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *copy = lv_obj_create(header);
    lv_obj_remove_style_all(copy);
    configure_column(copy, 4);
    create_label(copy, label, hapanel_ui_font_static_26(),
                 lv_color_hex(profile->theme.text_primary));
    create_label(copy, "Home detail", hapanel_ui_font_static_12(),
                 lv_color_hex(profile->theme.text_muted));

    lv_obj_t *home_target =
        create_pill_label(header, "Home", lv_color_hex(profile->theme.text_muted));
    make_page_target(home_target, HAPANEL_UI_PAGE_HOME);

    lv_obj_t *panel = create_panel(root);
    lv_obj_set_height(panel, 250);
    lv_obj_set_style_pad_all(panel, profile->spacing.md, 0);
    lv_obj_set_style_pad_row(panel, profile->spacing.md, 0);

    home_detail_view.accent_dot = lv_obj_create(panel);
    lv_obj_remove_style_all(home_detail_view.accent_dot);
    lv_obj_set_size(home_detail_view.accent_dot, 10, 10);
    lv_obj_set_style_radius(home_detail_view.accent_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(home_detail_view.accent_dot,
                              online ? lv_color_hex(entity_accents[entity_id])
                                     : lv_color_hex(profile->theme.status_offline),
                              0);
    lv_obj_set_style_bg_opa(home_detail_view.accent_dot, LV_OPA_COVER, 0);

    home_detail_view.value_label =
        create_dynamic_label(panel,
                             label,
                             value,
                             online ? lv_color_hex(profile->theme.text_primary)
                                    : lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(home_detail_view.value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(home_detail_view.value_label, LV_PCT(100));
    lv_obj_set_style_text_line_space(home_detail_view.value_label, 3, 0);

    const size_t detail_count = entity != NULL ? entity->detail_count : 0;
    home_detail_view.detail_count = detail_count < HAPANEL_HOME_DETAIL_ITEM_COUNT
                                        ? detail_count
                                        : HAPANEL_HOME_DETAIL_ITEM_COUNT;
    for (size_t i = 0; i < home_detail_view.detail_count; ++i) {
        create_home_detail_row(panel,
                               entity != NULL ? &entity->details[i] : NULL,
                               i,
                               lv_color_hex(entity_accents[entity_id]));
    }

    home_detail_view.created = true;
    enable_page_gestures(root);
    refresh_home_detail_page(status);
}

static void show_placeholder_page(const hapanel_ui_status_t *status,
                                  const char *title,
                                  const char *subtitle,
                                  const char *primary,
                                  const char *secondary,
                                  uint32_t accent)
{
    const hapanel_profile_t *profile = ui_profile();

    reset_views();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(profile->theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    configure_screen_root(root);

    create_status_bar(root, status, &static_page_view.psram_label, NULL);

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 74);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *copy = lv_obj_create(header);
    lv_obj_remove_style_all(copy);
    lv_obj_set_width(copy, 270);
    configure_column(copy, 4);
    create_label(copy, title, hapanel_ui_font_static_26(),
                 lv_color_hex(profile->theme.text_primary));
    lv_obj_t *subtitle_label =
        create_label(copy, subtitle, hapanel_ui_font_static_12(),
                     lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle_label, LV_PCT(100));

    lv_obj_t *root_target =
        create_pill_label(header, "Root", lv_color_hex(profile->theme.text_muted));
    make_page_target(root_target, HAPANEL_UI_PAGE_ROOT);

    lv_obj_t *body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 292);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, profile->spacing.sm, 0);

    lv_obj_t *dot = lv_obj_create(body);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    create_label(body, primary, hapanel_ui_font_static_18(),
                 lv_color_hex(profile->theme.text_primary));
    lv_obj_t *secondary_label =
        create_label(body, secondary, hapanel_ui_font_static_12(),
                     lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(secondary_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(secondary_label, 320);
    lv_obj_set_style_text_align(secondary_label, LV_TEXT_ALIGN_CENTER, 0);

    static_page_view.created = true;
    enable_page_gestures(root);
    refresh_static_page(status);
}

static void show_security_page(const hapanel_ui_status_t *status)
{
    show_placeholder_page(status,
                          "Security",
                          "Sensors, alerts, and perimeter state will live here.",
                          "No alerts",
                          "Security entities are reserved for the next Home Assistant slice.",
                          0xf0c674);
}

static void show_apps_page(const hapanel_ui_status_t *status)
{
    show_placeholder_page(status,
                          "Apps",
                          "Small panel utilities and optional tools will live here.",
                          "No apps pinned",
                          "App surfaces are scaffolded so Root has a stable right side.",
                          0x9ccfd8);
}

static void refresh_root_page(const hapanel_ui_status_t *status)
{
    if (!ambient_view.created || status == NULL) {
        return;
    }

    const hapanel_profile_t *profile = ui_profile();
    if (ambient_view.psram_label != NULL) {
        lv_label_set_text(ambient_view.psram_label, status->psram_ready ? "PSRAM" : "RAM");
        lv_obj_set_style_text_color(ambient_view.psram_label,
                                    status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                       : lv_color_hex(profile->theme.status_warning),
                                    0);
    }

    if (ambient_view.clock_label != NULL) {
        char clock_text[8];
        format_uptime_clock(status->uptime_ms, clock_text, sizeof(clock_text));
        lv_label_set_text(ambient_view.clock_label, clock_text);
    }

    if (ambient_view.uptime_label != NULL) {
        char uptime_text[24];
        format_uptime_caption(status->uptime_ms, uptime_text, sizeof(uptime_text));
        lv_label_set_text(ambient_view.uptime_label, uptime_text);
    }
}

static void refresh_system_status_page(const hapanel_ui_status_t *status)
{
    if (!root_view.created || status == NULL) {
        return;
    }

    if (root_view.psram_label != NULL) {
        const hapanel_profile_t *profile = ui_profile();
        lv_label_set_text(root_view.psram_label, status->psram_ready ? "PSRAM" : "RAM");
        lv_obj_set_style_text_color(root_view.psram_label,
                                    status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                       : lv_color_hex(profile->theme.status_warning),
                                    0);
    }

    const size_t row_count = status->item_count < root_view.row_count
                                 ? status->item_count
                                 : root_view.row_count;
    for (size_t i = 0; i < row_count; ++i) {
        if (root_view.status_values[i] != NULL) {
            hapanel_ui_font_log_missing_glyphs(status->items[i].label, status->items[i].value,
                                              hapanel_ui_font_dynamic_16());
            char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
            const lv_font_t *font = hapanel_ui_font_prepare_dynamic_text(status->items[i].value,
                                                                         display_text,
                                                                         sizeof(display_text));
            lv_obj_set_style_text_font(root_view.status_values[i], font, 0);
            lv_label_set_text(root_view.status_values[i], display_text);
            lv_obj_set_style_text_color(root_view.status_values[i],
                                        color_for_status(status->items[i].level),
                                        0);
        }

        if (root_view.status_dots[i] != NULL) {
            lv_obj_set_style_bg_color(root_view.status_dots[i],
                                      color_for_status(status->items[i].level),
                                      0);
        }
    }
}

static void refresh_home_page(const hapanel_ui_status_t *status)
{
    if (!home_view.created || status == NULL) {
        return;
    }

    const hapanel_profile_t *profile = ui_profile();
    if (home_view.psram_label != NULL) {
        lv_label_set_text(home_view.psram_label, status->psram_ready ? "PSRAM" : "RAM");
        lv_obj_set_style_text_color(home_view.psram_label,
                                    status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                       : lv_color_hex(profile->theme.status_warning),
                                    0);
    }

    const uint32_t entity_accents[HAPANEL_HOME_ENTITY_COUNT] = {
        [HAPANEL_HOME_ENTITY_SCENE] = 0x7ec8e3,
        [HAPANEL_HOME_ENTITY_LIGHTS] = 0xf0c674,
        [HAPANEL_HOME_ENTITY_CLIMATE] = 0x9ccfd8,
    };
    for (size_t i = 0; i < HAPANEL_HOME_ENTITY_COUNT; ++i) {
        const hapanel_home_entity_t *entity =
            home_state != NULL ? &home_state->entities[i] : NULL;
        if (entity == NULL) {
            continue;
        }

        if (home_view.entity_values[i] != NULL) {
            hapanel_ui_font_log_missing_glyphs(entity->label,
                                              entity->value,
                                              hapanel_ui_font_dynamic_16());
            char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
            const lv_font_t *font =
                hapanel_ui_font_prepare_dynamic_text(entity->value, display_text, sizeof(display_text));
            lv_obj_set_style_text_font(home_view.entity_values[i], font, 0);
            lv_label_set_text(home_view.entity_values[i], display_text);
            lv_obj_set_style_text_color(home_view.entity_values[i],
                                        entity->online
                                            ? lv_color_hex(profile->theme.text_primary)
                                            : lv_color_hex(profile->theme.text_muted),
                                        0);
        }

        if (home_view.entity_dots[i] != NULL) {
            lv_obj_set_style_bg_color(home_view.entity_dots[i],
                                      entity->online ? lv_color_hex(entity_accents[i])
                                                     : lv_color_hex(profile->theme.status_offline),
                                      0);
        }
    }

}

static void refresh_home_detail_page(const hapanel_ui_status_t *status)
{
    if (!home_detail_view.created || status == NULL) {
        return;
    }

    const hapanel_profile_t *profile = ui_profile();
    const hapanel_home_entity_t *entity = home_entity_or_null(home_detail_view.entity);
    if (home_detail_view.psram_label != NULL) {
        lv_label_set_text(home_detail_view.psram_label, status->psram_ready ? "PSRAM" : "RAM");
        lv_obj_set_style_text_color(home_detail_view.psram_label,
                                    status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                       : lv_color_hex(profile->theme.status_warning),
                                    0);
    }

    if (entity == NULL) {
        return;
    }

    const uint32_t entity_accents[HAPANEL_HOME_ENTITY_COUNT] = {
        [HAPANEL_HOME_ENTITY_SCENE] = 0x7ec8e3,
        [HAPANEL_HOME_ENTITY_LIGHTS] = 0xf0c674,
        [HAPANEL_HOME_ENTITY_CLIMATE] = 0x9ccfd8,
    };

    if (home_detail_view.accent_dot != NULL) {
        lv_obj_set_style_bg_color(home_detail_view.accent_dot,
                                  entity->online ? lv_color_hex(entity_accents[home_detail_view.entity])
                                                 : lv_color_hex(profile->theme.status_offline),
                                  0);
    }

    if (home_detail_view.value_label != NULL) {
        char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
        const lv_font_t *font =
            hapanel_ui_font_prepare_dynamic_text(entity->value, display_text, sizeof(display_text));
        hapanel_ui_font_log_missing_glyphs(entity->label, entity->value, font);
        lv_obj_set_style_text_font(home_detail_view.value_label, font, 0);
        lv_label_set_text(home_detail_view.value_label, display_text);
        lv_obj_set_style_text_color(home_detail_view.value_label,
                                    entity->online ? lv_color_hex(profile->theme.text_primary)
                                                   : lv_color_hex(profile->theme.text_muted),
                                    0);
    }

    const size_t detail_count = entity->detail_count < HAPANEL_HOME_DETAIL_ITEM_COUNT
                                    ? entity->detail_count
                                    : HAPANEL_HOME_DETAIL_ITEM_COUNT;
    if (home_detail_view.detail_count != detail_count) {
        show_home_detail_page(status, home_detail_view.entity);
        return;
    }

    for (size_t i = 0; i < detail_count; ++i) {
        const hapanel_home_detail_item_t *item = &entity->details[i];
        const lv_color_t item_color = item->online
                                          ? lv_color_hex(profile->theme.text_primary)
                                          : lv_color_hex(profile->theme.text_muted);

        if (home_detail_view.detail_dots[i] != NULL) {
            lv_obj_set_style_bg_color(home_detail_view.detail_dots[i],
                                      item->online
                                          ? lv_color_hex(entity_accents[home_detail_view.entity])
                                          : lv_color_hex(profile->theme.status_offline),
                                      0);
        }

        if (home_detail_view.detail_labels[i] != NULL) {
            char label_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
            const lv_font_t *font = hapanel_ui_font_prepare_dynamic_text(item->label,
                                                                         label_text,
                                                                         sizeof(label_text));
            hapanel_ui_font_log_missing_glyphs("Home detail label", item->label, font);
            lv_obj_set_style_text_font(home_detail_view.detail_labels[i], font, 0);
            lv_label_set_text(home_detail_view.detail_labels[i], label_text);
        }

        if (home_detail_view.detail_values[i] != NULL) {
            char value_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
            const lv_font_t *font = hapanel_ui_font_prepare_dynamic_text(item->value,
                                                                         value_text,
                                                                         sizeof(value_text));
            hapanel_ui_font_log_missing_glyphs(item->label, item->value, font);
            lv_obj_set_style_text_font(home_detail_view.detail_values[i], font, 0);
            lv_label_set_text(home_detail_view.detail_values[i], value_text);
            lv_obj_set_style_text_color(home_detail_view.detail_values[i], item_color, 0);
        }
    }
}

static void refresh_static_page(const hapanel_ui_status_t *status)
{
    if (!static_page_view.created || status == NULL) {
        return;
    }

    const hapanel_profile_t *profile = ui_profile();
    if (static_page_view.psram_label != NULL) {
        lv_label_set_text(static_page_view.psram_label, status->psram_ready ? "PSRAM" : "RAM");
        lv_obj_set_style_text_color(static_page_view.psram_label,
                                    status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                       : lv_color_hex(profile->theme.status_warning),
                                    0);
    }
}

const hapanel_ui_page_descriptor_t *hapanel_ui_page_descriptor(hapanel_ui_page_id_t page)
{
    if (page >= HAPANEL_UI_PAGE_COUNT) {
        return NULL;
    }

    return &page_descriptors[page];
}

hapanel_ui_page_id_t hapanel_ui_current_page(void)
{
    return current_page;
}

hapanel_ui_layer_t hapanel_ui_current_layer(void)
{
    return current_layer;
}

void hapanel_ui_set_page_request_callback(hapanel_ui_page_request_callback_t callback,
                                          void *context)
{
    page_request_callback = callback;
    page_request_context = context;
}

void hapanel_ui_set_home_action_callback(hapanel_ui_home_action_callback_t callback,
                                         void *context)
{
    home_action_callback = callback;
    home_action_context = context;
}

void hapanel_ui_show_page(hapanel_ui_page_id_t page, const hapanel_ui_status_t *status)
{
    const hapanel_ui_page_descriptor_t *descriptor = hapanel_ui_page_descriptor(page);
    if (descriptor == NULL) {
        return;
    }

    current_page = descriptor->id;
    current_layer = descriptor->layer;

    switch (page) {
    case HAPANEL_UI_PAGE_ROOT:
        show_root_page(status);
        break;
    case HAPANEL_UI_PAGE_HOME:
        show_home_page(status);
        break;
    case HAPANEL_UI_PAGE_HOME_SCENE:
    case HAPANEL_UI_PAGE_HOME_LIGHTS:
    case HAPANEL_UI_PAGE_HOME_CLIMATE:
        show_home_detail_page(status, entity_for_home_detail_page(page));
        break;
    case HAPANEL_UI_PAGE_SECURITY:
        show_security_page(status);
        break;
    case HAPANEL_UI_PAGE_APPS:
        show_apps_page(status);
        break;
    case HAPANEL_UI_PAGE_SYSTEM_STATUS:
    default:
        show_system_status_page(status);
        break;
    }
}

void hapanel_ui_set_home_state(const hapanel_home_state_t *state)
{
    home_state = state;
}

void hapanel_ui_refresh_current_page(const hapanel_ui_status_t *status)
{
    switch (current_page) {
    case HAPANEL_UI_PAGE_ROOT:
        refresh_root_page(status);
        break;
    case HAPANEL_UI_PAGE_HOME:
        refresh_home_page(status);
        break;
    case HAPANEL_UI_PAGE_HOME_SCENE:
    case HAPANEL_UI_PAGE_HOME_LIGHTS:
    case HAPANEL_UI_PAGE_HOME_CLIMATE:
        refresh_home_detail_page(status);
        break;
    case HAPANEL_UI_PAGE_SECURITY:
    case HAPANEL_UI_PAGE_APPS:
        refresh_static_page(status);
        break;
    case HAPANEL_UI_PAGE_SYSTEM_STATUS:
    default:
        refresh_system_status_page(status);
        break;
    }
}

void hapanel_ui_show_root(const hapanel_ui_status_t *status)
{
    hapanel_ui_show_page(HAPANEL_UI_PAGE_ROOT, status);
}

void hapanel_ui_refresh_root(const hapanel_ui_status_t *status)
{
    hapanel_ui_refresh_current_page(status);
}
