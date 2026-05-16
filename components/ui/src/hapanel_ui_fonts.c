#include "hapanel_ui_fonts.h"

LV_FONT_DECLARE(hapanel_font_dynamic_16);

const lv_font_t *hapanel_ui_font_static_12(void)
{
    return &lv_font_montserrat_12;
}

const lv_font_t *hapanel_ui_font_static_16(void)
{
    return &lv_font_montserrat_16;
}

const lv_font_t *hapanel_ui_font_static_18(void)
{
    return &lv_font_montserrat_18;
}

const lv_font_t *hapanel_ui_font_static_26(void)
{
    return &lv_font_montserrat_26;
}

const lv_font_t *hapanel_ui_font_dynamic_16(void)
{
    return &hapanel_font_dynamic_16;
}
