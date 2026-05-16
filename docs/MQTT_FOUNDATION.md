# MQTT Foundation

HAPanel's Home Assistant integration starts with MQTT.

Current behavior:
- exposes local Kconfig settings for broker URI, client ID, username, and
  password
- reports MQTT status to the system status UI
- remains functional when MQTT is not configured
- reports `Client missing` if a broker is configured before the MQTT client
  dependency is linked

Current limitation:
- no broker connection is attempted yet because this ESP-IDF install does not
  include the ESP-MQTT component in the active component set
- no Home Assistant discovery, entity state, command, or availability topics
  are implemented yet

The next MQTT step is to add the ESP-MQTT dependency, then connect only after
Wi-Fi is online and publish a minimal availability/status message.
