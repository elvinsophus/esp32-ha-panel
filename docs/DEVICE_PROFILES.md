# Device Profiles

The project should support multiple ESP32 boards and display resolutions.

## Profile Types

- hardware profile
- layout profile
- performance profile
- theme profile

## Example Configuration

display:
  width: 480
  height: 480
  shape: square

layout:
  status_bar_height: 36
  outer_margin: 24
  card_gap: 16

animation:
  tier: smooth

## Principle

Hardware profiles should influence presentation, not information architecture.
