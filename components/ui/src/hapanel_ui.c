#include "hapanel_ui.h"

#include "hapanel_profile.h"
#include "lvgl.h"

typedef struct {
    bool created;
    size_t row_count;
    lv_obj_t *psram_label;
    lv_obj_t *wifi_label;
    lv_obj_t *status_dots[HAPANEL_UI_STATUS_MAX_ITEMS];
    lv_obj_t *status_values[HAPANEL_UI_STATUS_MAX_ITEMS];
} hapanel_root_view_t;

static hapanel_root_view_t root_view;

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

static void configure_column(lv_obj_t *obj, int32_t row_gap)
{
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(obj, row_gap, 0);
}

static void create_status_bar(lv_obj_t *parent, const hapanel_ui_status_t *status)
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

    create_label(bar, "HAPanel", &lv_font_montserrat_16, lv_color_hex(profile->theme.text_primary));

    lv_obj_t *right = lv_obj_create(bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, LV_SIZE_CONTENT);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, profile->spacing.sm, 0);

    root_view.psram_label = create_label(right,
                                         status->psram_ready ? "PSRAM" : "RAM",
                                         &lv_font_montserrat_12,
                                         status->psram_ready ? lv_color_hex(profile->theme.status_ok)
                                                            : lv_color_hex(profile->theme.status_warning));
    root_view.wifi_label = create_label(right, "Wi-Fi", &lv_font_montserrat_12,
                                        lv_color_hex(profile->theme.text_muted));
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

    create_label(left, item->label, &lv_font_montserrat_16, lv_color_hex(0xb7c1cd));
    lv_obj_t *value = create_label(row, item->value, &lv_font_montserrat_16,
                                   lv_color_hex(0xf2f6fa));

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

void hapanel_ui_show_root(const hapanel_ui_status_t *status)
{
    const hapanel_profile_t *profile = ui_profile();

    root_view = (hapanel_root_view_t){0};

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(profile->theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(root, profile->spacing.sm, 0);
    configure_column(root, profile->spacing.sm);

    create_status_bar(root, status);

    lv_obj_t *hero = lv_obj_create(root);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 72);
    lv_obj_set_layout(hero, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 4, 0);

    create_label(hero, "HAPanel", &lv_font_montserrat_26, lv_color_hex(profile->theme.text_primary));
    create_label(hero, profile->board.name, &lv_font_montserrat_16,
                 lv_color_hex(profile->theme.text_muted));

    lv_obj_t *panel = create_panel(root);
    create_label(panel, "System Status", &lv_font_montserrat_18,
                 lv_color_hex(profile->theme.text_primary));

    const size_t row_count = status->item_count < HAPANEL_UI_STATUS_MAX_ITEMS
                                 ? status->item_count
                                 : HAPANEL_UI_STATUS_MAX_ITEMS;
    root_view.row_count = row_count;

    for (size_t i = 0; i < row_count; ++i) {
        create_status_row(panel, &status->items[i], i);
    }

    lv_obj_t *footer = create_label(root, "Core services will appear here as they come online.",
                                    &lv_font_montserrat_12,
                                    lv_color_hex(profile->theme.text_muted));
    lv_label_set_long_mode(footer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(footer, LV_PCT(100));

    root_view.created = true;
    hapanel_ui_refresh_root(status);
}

void hapanel_ui_refresh_root(const hapanel_ui_status_t *status)
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
            lv_label_set_text(root_view.status_values[i], status->items[i].value);
        }

        if (root_view.status_dots[i] != NULL) {
            lv_obj_set_style_bg_color(root_view.status_dots[i],
                                      color_for_status(status->items[i].level),
                                      0);
        }
    }
}
