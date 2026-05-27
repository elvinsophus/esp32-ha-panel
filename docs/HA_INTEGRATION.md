# Home Assistant Integration

The panel integrates tightly with Home Assistant.

Preferred architecture:
1. MQTT first
2. Native HA WebSocket later if needed

The system should:
- gracefully reconnect
- remain functional while offline
- cache useful states locally

Future integrations:
- Frigate
- plant monitoring
- cameras
- scenes
- notifications

## MQTT Command Bridge

HAPanel does not execute Home Assistant services directly. Detail-row taps on
the Home pages publish compact JSON command requests to
`hapanel/home/command`. Home Assistant owns the automation that decides whether
the request is allowed and which service to call.

The firmware currently publishes this request shape:

```json
{"schema":"hapanel.home_command.v1","entity":"lights","category":"Lights","detail_index":0,"label":"Kitchen","target":"light.kitchen","action":"toggle"}
```

Use [examples/home-assistant/hapanel_command_bridge.yaml](../examples/home-assistant/hapanel_command_bridge.yaml)
as the first bridge package. It subscribes to `hapanel/home/command`, accepts
only `hapanel.home_command.v1`, limits targets to common controllable domains,
and maps `toggle`, `turn_on`, `turn_off`, and scene/script `activate` requests
to generic `homeassistant.*` actions.

This keeps the trust boundary simple:
- firmware publishes intent
- Home Assistant validates the target and action
- Home Assistant executes the service
- retained entity state topics update the panel afterwards

## Example Detail Payload

The command bridge expects Home detail rows to include a target after the
visible label/value text:

```text
Kitchen on
Kitchen: On | light.kitchen | toggle
Dining: Off | light.dining | toggle
Evening: Ready | scene.evening | activate
```

The first line is the tile summary. Each following row is displayed on the
detail page. The first metadata field is the Home Assistant target entity. The
second metadata field is optional and defaults to `toggle` in firmware.
