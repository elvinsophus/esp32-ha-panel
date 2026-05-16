# MQTT Foundation

HAPanel's Home Assistant integration starts with MQTT.

Current behavior:
- exposes local Kconfig settings for broker URI, client ID, username, and
  password
- reports MQTT status to the system status UI
- remains functional when MQTT is not configured
- starts ESP-MQTT when a broker URI is configured
- reports connecting, connected, disconnected, and error states
- waits for Wi-Fi/IP before starting the MQTT client
- publishes retained `online` availability on connect
- configures a retained `offline` last-will message

Current limitation:
- no Home Assistant discovery, entity state, command, or availability topics
  beyond basic availability are implemented yet

The next MQTT step is to publish a minimal structured device status topic, then
add Home Assistant discovery only after the topic model is stable.
