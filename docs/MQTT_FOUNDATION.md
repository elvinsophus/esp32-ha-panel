# MQTT Foundation

HAPanel's Home Assistant integration starts with MQTT.

Current behavior:
- exposes local Kconfig settings for broker URI, client ID, username, and
  password
- reports MQTT status to the system status UI
- remains functional when MQTT is not configured
- starts ESP-MQTT when a broker URI is configured
- reports connecting, connected, disconnected, and error states

Current limitation:
- MQTT starts after the network service starts, but it does not yet wait for a
  confirmed Wi-Fi/IP-online event
- no Home Assistant discovery, entity state, command, or availability topics
  are implemented yet

The next MQTT step is to connect only after Wi-Fi is online, then publish a
minimal availability/status message.
