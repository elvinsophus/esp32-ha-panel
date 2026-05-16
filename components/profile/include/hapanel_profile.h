#pragma once

#include <stdint.h>

typedef struct {
    const char *id;
    const char *name;
} hapanel_board_profile_t;

typedef struct {
    const char *id;
    uint16_t width;
    uint16_t height;
    uint8_t status_bar_height;
} hapanel_layout_profile_t;

typedef struct {
    const char *id;
    const char *animation_tier;
} hapanel_performance_profile_t;

typedef struct {
    const char *id;
    uint32_t background;
    uint32_t surface;
    uint32_t surface_border;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_muted;
    uint32_t status_ok;
    uint32_t status_pending;
    uint32_t status_warning;
    uint32_t status_error;
    uint32_t status_offline;
} hapanel_theme_profile_t;

typedef struct {
    uint8_t xs;
    uint8_t sm;
    uint8_t md;
    uint8_t lg;
    uint8_t xl;
} hapanel_spacing_tokens_t;

typedef struct {
    uint8_t sm;
    uint8_t md;
    uint8_t lg;
} hapanel_radius_tokens_t;

typedef struct {
    uint8_t status;
    uint8_t small;
    uint8_t medium;
    uint8_t large;
    uint8_t hero;
} hapanel_icon_tokens_t;

typedef struct {
    hapanel_board_profile_t board;
    hapanel_layout_profile_t layout;
    hapanel_performance_profile_t performance;
    hapanel_theme_profile_t theme;
    hapanel_spacing_tokens_t spacing;
    hapanel_radius_tokens_t radius;
    hapanel_icon_tokens_t icon;
} hapanel_profile_t;

const hapanel_profile_t *hapanel_profile_active(void);
