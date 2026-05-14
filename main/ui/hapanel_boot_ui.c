#include "hapanel_boot_ui.h"

#include "lvgl.h"

typedef struct {
    const char *label;
    const char *value;
    lv_color_t accent;
} hapanel_status_row_t;

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

static void create_status_row(lv_obj_t *parent, const hapanel_status_row_t *status)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
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
    lv_obj_set_style_pad_column(left, 12, 0);

    lv_obj_t *dot = lv_obj_create(left);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, status->accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    create_label(left, status->label, &lv_font_montserrat_16, lv_color_hex(0xb7c1cd));
    create_label(row, status->value, &lv_font_montserrat_16, lv_color_hex(0xf2f6fa));
}

void hapanel_boot_ui_show(void)
{
    static const hapanel_status_row_t statuses[] = {
        {.label = "Display", .value = "Ready", .accent = LV_COLOR_MAKE(0x5d, 0xd3, 0x9e)},
        {.label = "Touch", .value = "BSP online", .accent = LV_COLOR_MAKE(0x5d, 0xd3, 0x9e)},
        {.label = "Wi-Fi", .value = "Not configured", .accent = LV_COLOR_MAKE(0xe6, 0xb8, 0x5c)},
        {.label = "MQTT", .value = "Offline", .accent = LV_COLOR_MAKE(0x8f, 0x9a, 0xa8)},
        {.label = "OTA", .value = "Pending", .accent = LV_COLOR_MAKE(0x8f, 0x9a, 0xa8)},
    };

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0b1017), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(root, 36, 0);
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root, 24, 0);

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(header, 10, 0);

    create_label(header, "HAPanel", &lv_font_montserrat_26, lv_color_hex(0xf4f7fb));
    create_label(header, "Foundation firmware", &lv_font_montserrat_16, lv_color_hex(0x7f8b99));

    lv_obj_t *status_panel = lv_obj_create(root);
    lv_obj_remove_style_all(status_panel);
    lv_obj_set_width(status_panel, LV_PCT(100));
    lv_obj_set_height(status_panel, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(status_panel, 24, 0);
    lv_obj_set_style_radius(status_panel, 8, 0);
    lv_obj_set_style_bg_color(status_panel, lv_color_hex(0x151b24), 0);
    lv_obj_set_style_bg_opa(status_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_panel, 1, 0);
    lv_obj_set_style_border_color(status_panel, lv_color_hex(0x253141), 0);
    lv_obj_set_layout(status_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(status_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(status_panel, 8, 0);

    create_label(status_panel, "System Status", &lv_font_montserrat_18, lv_color_hex(0xf2f6fa));

    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        create_status_row(status_panel, &statuses[i]);
    }

    lv_obj_t *footer = create_label(root, "Boot complete. Core services will appear here as they come online.",
                                    &lv_font_montserrat_12, lv_color_hex(0x6f7a86));
    lv_label_set_long_mode(footer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(footer, LV_PCT(100));
}
