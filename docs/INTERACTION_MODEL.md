# Interaction Model

One-finger swipe:
- Root -> Home Status: swipe right
- Root -> Security: swipe up
- Root -> Apps: swipe left
- Home Status -> Root: swipe left
- Security -> Root: swipe down
- Apps -> Root: swipe right
- Home detail -> Home Status: swipe right
- Home detail row tap: publish a Home action event over MQTT

The current firmware implements this first spatial swipe set using LVGL gesture
events. Status/quick-action gestures remain deferred so the top edge does not
conflict with Root navigation.

Two-finger pinch:
- return to parent/root

Swipe down left:
- notifications

Swipe down right:
- quick actions

Long press:
- context actions

Assistant activation:
- wake word
- assistant button
- gesture shortcut

Interactions should feel predictable and spatially consistent.
