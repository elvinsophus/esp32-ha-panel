#include "hapanel_ui.h"

#include "hapanel_profile.h"
#include "hapanel_ui_fonts.h"
#include "lvgl.h"

#include <string.h>

enum {
    HAPANEL_UI_DYNAMIC_TEXT_MAX = 128,
};

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
    lv_obj_t *wifi_value;
    lv_obj_t *mqtt_value;
    lv_obj_t *ota_value;
} hapanel_home_view_t;

static hapanel_root_view_t root_view;
static hapanel_home_view_t home_view;
static const hapanel_home_state_t *home_state;

static void show_system_status_page(const hapanel_ui_status_t *status);
static void refresh_system_status_page(const hapanel_ui_status_t *status);
static void show_home_page(const hapanel_ui_status_t *status);
static void refresh_home_page(const hapanel_ui_status_t *status);

static const hapanel_ui_page_descriptor_t page_descriptors[HAPANEL_UI_PAGE_COUNT] = {
    [HAPANEL_UI_PAGE_SYSTEM_STATUS] = {
        .id = HAPANEL_UI_PAGE_SYSTEM_STATUS,
        .layer = HAPANEL_UI_LAYER_ROOT,
        .name = "System Status",
    },
    [HAPANEL_UI_PAGE_HOME] = {
        .id = HAPANEL_UI_PAGE_HOME,
        .layer = HAPANEL_UI_LAYER_ROOT,
        .name = "Home",
    },
};

static hapanel_ui_page_id_t current_page = HAPANEL_UI_PAGE_SYSTEM_STATUS;
static hapanel_ui_layer_t current_layer = HAPANEL_UI_LAYER_ROOT;

static const hapanel_profile_t *ui_profile(void)
{
    return hapanel_profile_active();
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
    const lv_font_t *font = hapanel_ui_font_dynamic_16();
    hapanel_ui_font_log_missing_glyphs(surface, text, font);
    char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
    hapanel_ui_font_prepare_dynamic_text(text, display_text, sizeof(display_text));
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

static const hapanel_ui_status_item_t *find_status_item(const hapanel_ui_status_t *status,
                                                        const char *label)
{
    if (status == NULL || label == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < status->item_count; ++i) {
        if (status->items[i].label != NULL && strcmp(status->items[i].label, label) == 0) {
            return &status->items[i];
        }
    }

    return NULL;
}

static const char *status_value_or(const hapanel_ui_status_t *status,
                                   const char *label,
                                   const char *fallback)
{
    const hapanel_ui_status_item_t *item = find_status_item(status, label);
    return item != NULL && item->value != NULL ? item->value : fallback;
}

static hapanel_ui_status_level_t status_level_or(const hapanel_ui_status_t *status,
                                                 const char *label,
                                                 hapanel_ui_status_level_t fallback)
{
    const hapanel_ui_status_item_t *item = find_status_item(status, label);
    return item != NULL ? item->level : fallback;
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
    lv_obj_set_size(tile, 204, 100);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_set_style_radius(tile, profile->radius.sm, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(profile->theme.surface), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(profile->theme.surface_border), 0);
    configure_column(tile, 5);

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
    lv_obj_set_height(value_obj, 42);
    lv_obj_set_style_text_line_space(value_obj, 2, 0);

    if (value_label != NULL) {
        *value_label = value_obj;
    }

    return tile;
}

static void show_system_status_page(const hapanel_ui_status_t *status)
{
    const hapanel_profile_t *profile = ui_profile();

    root_view = (hapanel_root_view_t){0};
    home_view = (hapanel_home_view_t){0};

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
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 4, 0);

    create_label(hero, "HAPanel", hapanel_ui_font_static_26(),
                 lv_color_hex(profile->theme.text_primary));
    create_label(hero, profile->board.name, hapanel_ui_font_static_16(),
                 lv_color_hex(profile->theme.text_muted));

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
    refresh_system_status_page(status);
}

static void show_home_page(const hapanel_ui_status_t *status)
{
    const hapanel_profile_t *profile = ui_profile();

    root_view = (hapanel_root_view_t){0};
    home_view = (hapanel_home_view_t){0};

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
    lv_obj_set_height(header, 48);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 2, 0);

    create_label(header, "Home", hapanel_ui_font_static_26(),
                 lv_color_hex(profile->theme.text_primary));

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, 332);
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
        create_home_tile(grid,
                         entity != NULL ? entity->label : "Entity",
                         entity != NULL ? entity->value : "Unknown",
                         entity != NULL && entity->online
                             ? lv_color_hex(entity_accents[i])
                             : lv_color_hex(profile->theme.status_offline),
                         &home_view.entity_dots[i],
                         &home_view.entity_values[i]);
    }
    create_home_tile(grid,
                     "Wi-Fi",
                     status_value_or(status, "Wi-Fi", "Offline"),
                     color_for_status(status_level_or(status, "Wi-Fi", HAPANEL_UI_STATUS_OFFLINE)),
                     NULL,
                     &home_view.wifi_value);
    create_home_tile(grid,
                     "MQTT",
                     status_value_or(status, "MQTT", "Offline"),
                     color_for_status(status_level_or(status, "MQTT", HAPANEL_UI_STATUS_OFFLINE)),
                     NULL,
                     &home_view.mqtt_value);
    create_home_tile(grid,
                     "OTA",
                     status_value_or(status, "OTA", "Unknown"),
                     color_for_status(status_level_or(status, "OTA", HAPANEL_UI_STATUS_OFFLINE)),
                     NULL,
                     &home_view.ota_value);

    home_view.created = true;
    refresh_home_page(status);
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
            hapanel_ui_font_prepare_dynamic_text(status->items[i].value,
                                                 display_text,
                                                 sizeof(display_text));
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
            hapanel_ui_font_prepare_dynamic_text(entity->value, display_text, sizeof(display_text));
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

    if (home_view.wifi_value != NULL) {
        const char *value = status_value_or(status, "Wi-Fi", "Offline");
        hapanel_ui_font_log_missing_glyphs("Wi-Fi", value, hapanel_ui_font_dynamic_16());
        char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
        hapanel_ui_font_prepare_dynamic_text(value, display_text, sizeof(display_text));
        lv_label_set_text(home_view.wifi_value, display_text);
        lv_obj_set_style_text_color(home_view.wifi_value,
                                    color_for_status(status_level_or(status,
                                                                     "Wi-Fi",
                                                                     HAPANEL_UI_STATUS_OFFLINE)),
                                    0);
    }

    if (home_view.mqtt_value != NULL) {
        const char *value = status_value_or(status, "MQTT", "Offline");
        hapanel_ui_font_log_missing_glyphs("MQTT", value, hapanel_ui_font_dynamic_16());
        char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
        hapanel_ui_font_prepare_dynamic_text(value, display_text, sizeof(display_text));
        lv_label_set_text(home_view.mqtt_value, display_text);
        lv_obj_set_style_text_color(home_view.mqtt_value,
                                    color_for_status(status_level_or(status,
                                                                     "MQTT",
                                                                     HAPANEL_UI_STATUS_OFFLINE)),
                                    0);
    }

    if (home_view.ota_value != NULL) {
        const char *value = status_value_or(status, "OTA", "Unknown");
        hapanel_ui_font_log_missing_glyphs("OTA", value, hapanel_ui_font_dynamic_16());
        char display_text[HAPANEL_UI_DYNAMIC_TEXT_MAX];
        hapanel_ui_font_prepare_dynamic_text(value, display_text, sizeof(display_text));
        lv_label_set_text(home_view.ota_value, display_text);
        lv_obj_set_style_text_color(home_view.ota_value,
                                    color_for_status(status_level_or(status,
                                                                     "OTA",
                                                                     HAPANEL_UI_STATUS_OFFLINE)),
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

void hapanel_ui_show_page(hapanel_ui_page_id_t page, const hapanel_ui_status_t *status)
{
    const hapanel_ui_page_descriptor_t *descriptor = hapanel_ui_page_descriptor(page);
    if (descriptor == NULL) {
        return;
    }

    current_page = descriptor->id;
    current_layer = descriptor->layer;

    switch (page) {
    case HAPANEL_UI_PAGE_HOME:
        show_home_page(status);
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
    case HAPANEL_UI_PAGE_HOME:
        refresh_home_page(status);
        break;
    case HAPANEL_UI_PAGE_SYSTEM_STATUS:
    default:
        refresh_system_status_page(status);
        break;
    }
}

void hapanel_ui_show_root(const hapanel_ui_status_t *status)
{
    hapanel_ui_show_page(HAPANEL_UI_PAGE_SYSTEM_STATUS, status);
}

void hapanel_ui_refresh_root(const hapanel_ui_status_t *status)
{
    hapanel_ui_refresh_current_page(status);
}
