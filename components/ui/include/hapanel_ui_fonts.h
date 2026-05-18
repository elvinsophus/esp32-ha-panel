#pragma once

#include <stddef.h>

#include "lvgl.h"

const lv_font_t *hapanel_ui_font_static_12(void);
const lv_font_t *hapanel_ui_font_static_16(void);
const lv_font_t *hapanel_ui_font_static_18(void);
const lv_font_t *hapanel_ui_font_static_26(void);
const lv_font_t *hapanel_ui_font_dynamic_16(void);
const lv_font_t *hapanel_ui_font_prepare_dynamic_text(const char *input,
                                                      char *output,
                                                      size_t output_size);
void hapanel_ui_font_log_missing_glyphs(const char *surface, const char *text,
                                        const lv_font_t *font);
