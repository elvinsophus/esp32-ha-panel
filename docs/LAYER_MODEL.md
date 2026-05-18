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
- `HAPANEL_UI_PAGE_HOME`: root-layer Home Assistant surface scaffold with static
  tiles plus live foundation status tiles

The current page router is intentionally minimal. It gives future Home
Assistant pages a stable navigation target. During bring-up, MQTT commands
`ui_show_root`, `ui_show_home`, and `ui_show_status` switch between the first
root pages.
