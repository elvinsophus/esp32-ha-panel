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
- publishes retained Home Assistant MQTT discovery for early diagnostic
  entities when `CONFIG_HAPANEL_MQTT_HA_DISCOVERY_ENABLE` is enabled
- publishes retained Home Assistant MQTT discovery for diagnostic command
  buttons that use the existing safe command topic
- subscribes to `CONFIG_HAPANEL_MQTT_COMMAND_TOPIC` for safe foundation
  commands
- publishes non-retained command result JSON to
  `CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC`
- supports `{"command":"status_refresh"}` to republish device status, state,
  and discovery
- supports `{"command":"ui_refresh"}` to re-render the current status UI from
  runtime state
- supports an optional command `id` string for result correlation

Current limitation:
- Home Assistant discovery currently covers only low-risk diagnostic entities;
  control entities and richer state sensors are still intentionally deferred
- command handling is intentionally limited to low-risk foundation actions

The next MQTT step is to expand discovery around existing retained topics before
adding Home Assistant control commands.

## Home Assistant Discovery

When discovery is enabled, HAPanel publishes this retained config topic:

```text
homeassistant/sensor/hapanel_app_version/config
homeassistant/sensor/hapanel_uptime/config
homeassistant/sensor/hapanel_wifi_status/config
homeassistant/sensor/hapanel_mqtt_status/config
homeassistant/sensor/hapanel_ota_status/config
homeassistant/binary_sensor/hapanel_psram_ready/config
homeassistant/button/hapanel_status_refresh/config
homeassistant/button/hapanel_ui_refresh/config
```

The app-version entity reads from `CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC`.
The uptime, Wi-Fi status, MQTT status, OTA status, and PSRAM readiness entities
read from `CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`. Wi-Fi, MQTT, and OTA are
also exposed as top-level `wifi`, `mqtt`, and `ota` objects in the retained
state payload so discovery templates do not depend on service-array ordering.
All discovered entities use
`CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC` for online/offline availability and
group under the HAPanel device in Home Assistant.

The discovered buttons publish non-retained command payloads to
`CONFIG_HAPANEL_MQTT_COMMAND_TOPIC`:

```json
{"command":"status_refresh"}
{"command":"ui_refresh"}
```

## Local Bring-Up

Copy `sdkconfig.defaults.local.example` to `sdkconfig.defaults.local` and fill in
the Wi-Fi and MQTT values for the test environment. The local file is ignored by
Git and is loaded by CMake when present.

## Command Test

Use `tools/mqtt_command_test.ps1` from the repository root to publish a command
and wait for the matching command result:

```powershell
.\tools\mqtt_command_test.ps1 -Command status_refresh
.\tools\mqtt_command_test.ps1 -Command ui_refresh
.\tools\mqtt_command_test.ps1 -Command unknown_command -ExpectStatus rejected
```

The script reads broker, credential, command topic, and result topic settings
from `sdkconfig` and `sdkconfig.defaults.local`. It prints the JSON result
payload and exits with an error if the expected result does not arrive.

## Topic Dump

Use `tools/mqtt_topic_dump.ps1` to inspect retained state or discovery topics:

```powershell
.\tools\mqtt_topic_dump.ps1 -Json
.\tools\mqtt_topic_dump.ps1 -Topic homeassistant/sensor/hapanel_ota_status/config -Json
```

The script reads broker and credential settings from `sdkconfig` and
`sdkconfig.defaults.local`. Without `-Topic`, it reads
`CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`.
