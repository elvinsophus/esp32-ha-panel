#include "hapanel_profile.h"

static const hapanel_profile_t active_profile = {
    .board = {
        .id = "waveshare_esp32s3_touch_lcd_4b",
        .name = "Waveshare ESP32-S3-Touch-LCD-4B",
    },
    .layout = {
        .id = "square_480",
        .width = 480,
        .height = 480,
        .status_bar_height = 36,
    },
    .performance = {
        .id = "esp32s3_psram",
        .animation_tier = "smooth",
    },
    .theme = {
        .id = "default_dark",
        .background = 0x101214,
        .surface = 0x151b24,
        .surface_border = 0x253141,
        .text_primary = 0xf2f6fa,
        .text_secondary = 0xb7c1cd,
        .text_muted = 0x6f7a86,
        .status_ok = 0x5dd39e,
        .status_pending = 0xe6b85c,
        .status_warning = 0xf08f5f,
        .status_error = 0xff6b6b,
        .status_offline = 0x8f9aa8,
    },
    .spacing = {
        .xs = 8,
        .sm = 16,
        .md = 24,
        .lg = 36,
        .xl = 56,
    },
    .radius = {
        .sm = 12,
        .md = 20,
        .lg = 28,
    },
    .icon = {
        .status = 20,
        .small = 32,
        .medium = 48,
        .large = 56,
        .hero = 80,
    },
};

const hapanel_profile_t *hapanel_profile_active(void)
{
    return &active_profile;
}
