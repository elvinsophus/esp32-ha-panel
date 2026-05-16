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
