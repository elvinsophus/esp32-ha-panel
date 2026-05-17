# MQTT Foundation

HAPanel's Home Assistant integration starts with MQTT.

Current behavior:
- exposes local Kconfig settings for broker URI, client ID, username, and
  password
- supports an ignored `sdkconfig.defaults.local` file for local broker and
  network credentials
- reports MQTT status to the system status UI
- remains functional when MQTT is not configured
- starts ESP-MQTT when a broker URI is configured
- reports connecting, connected, disconnected, and error states
- waits for Wi-Fi/IP before starting the MQTT client
- publishes retained `online` availability on connect
- configures a retained `offline` last-will message
- publishes retained structured device status JSON to
  `CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC`
- publishes retained live device state JSON to
  `CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`
- subscribes to `CONFIG_HAPANEL_MQTT_COMMAND_TOPIC` for safe foundation
  commands
- publishes non-retained command result JSON to
  `CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC`
- supports `{"command":"status_refresh"}` to republish device status and state
- supports `{"command":"ui_refresh"}` to re-render the current status UI from
  runtime state
- supports an optional command `id` string for result correlation

Current limitation:
- no Home Assistant discovery or entity state topics are implemented yet
- command handling is intentionally limited to low-risk foundation actions

The next MQTT step is to add a tiny external test script for the command/result
round trip, then start shaping Home Assistant discovery only after the topic
model is stable.

## Local Bring-Up

Copy `sdkconfig.defaults.local.example` to `sdkconfig.defaults.local` and fill in
the Wi-Fi and MQTT values for the test environment. The local file is ignored by
Git and is loaded by CMake when present.
