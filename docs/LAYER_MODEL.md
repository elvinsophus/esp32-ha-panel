# Layer Model

## Ambient Layer
Idle/passive visual state.

## Root Layer
Primary spatial navigation root.

## Domain Layer
Major categorized functional layers.

## Detail Layer
Entity-specific interaction pages.

## Overlay Layer
Temporary non-blocking interface.

## Modal Layer
Interruptive high-priority content.

Layers are NOT equivalent to applications.

## Implemented Pages

The firmware now exposes a small page registry in `hapanel_ui_layer.h`.

Current pages:
- `HAPANEL_UI_PAGE_ROOT`: root spatial origin with a quiet ambient clock and
  assistant-presence scaffold
- `HAPANEL_UI_PAGE_SYSTEM_STATUS`: root-layer boot/status surface backed by the
  existing service status UI
- `HAPANEL_UI_PAGE_HOME`: domain-layer Home Status surface scaffold with live
  Home Assistant category tiles
- `HAPANEL_UI_PAGE_SECURITY`: domain-layer placeholder for security sensors and
  alerts
- `HAPANEL_UI_PAGE_APPS`: domain-layer placeholder for optional panel utilities
- `HAPANEL_UI_PAGE_HOME_SCENE`: detail-layer page for the Scene category
- `HAPANEL_UI_PAGE_HOME_LIGHTS`: detail-layer page for the Lights category
- `HAPANEL_UI_PAGE_HOME_CLIMATE`: detail-layer page for the Climate category

The current page router is intentionally minimal. It gives future Home
Assistant pages a stable navigation target. During bring-up, MQTT commands
`ui_show_root`, `ui_show_home`, `ui_show_security`, `ui_show_apps`, and
`ui_show_status` switch between the first pages.

The Root page also supports one-finger spatial gestures: right to Home Status,
up to Security, and left to Apps. The opposite gesture on each domain page
returns to Root.
Home Status category tiles open detail pages. A detail page returns to Home
Status through its fixed Home target or a right swipe. Detail pages show the
retained category summary plus a small fixed set of MQTT-fed detail rows.
