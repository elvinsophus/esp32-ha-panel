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
  `CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`, including read-only OTA preflight
  and partition inventory details
- publishes retained Home Assistant MQTT discovery for early diagnostic
  entities when `CONFIG_HAPANEL_MQTT_HA_DISCOVERY_ENABLE` is enabled
- publishes retained Home Assistant MQTT discovery for diagnostic command
  buttons that use the existing safe command topic
- publishes retained Home Assistant MQTT discovery for a last-command-result
  diagnostic sensor backed by the retained command state topic
- subscribes to `CONFIG_HAPANEL_MQTT_COMMAND_TOPIC` for safe foundation
  commands
- publishes non-retained command result JSON to
  `CONFIG_HAPANEL_MQTT_COMMAND_RESULT_TOPIC`
- publishes retained latest command result JSON to
  `CONFIG_HAPANEL_MQTT_COMMAND_STATE_TOPIC`
- supports `{"command":"status_refresh"}` to republish device status, state,
  and discovery
- supports `{"command":"ui_refresh"}` to re-render the current status UI from
  runtime state
- supports `{"command":"ota_preflight"}` to run the read-only OTA readiness
  check and report the current running and target OTA slots
- supports `{"command":"ota_update","url":"http://.../hapanel.bin"}` to
  download a firmware image over plain HTTP, write it to the inactive OTA slot,
  validate it, and set it as the next boot target without rebooting
- supports `{"command":"ota_reboot"}` to reboot only when
  `ota.inventory.reboot_required` is true
- rejects `{"command":"ota_self_test_stage"}` unless
  `CONFIG_HAPANEL_OTA_MQTT_SELF_TEST_STAGE_ENABLE` is explicitly enabled for a
  development build
- supports an optional command `id` string for result correlation

Current limitation:
- Home Assistant discovery currently covers only low-risk diagnostic entities;
  control entities and richer state sensors are still intentionally deferred
- OTA update command discovery is intentionally not published yet because it
  needs a URL input and should remain an explicit admin action

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
homeassistant/sensor/hapanel_ota_phase/config
homeassistant/binary_sensor/hapanel_ota_ready/config
homeassistant/sensor/hapanel_ota_running_slot/config
homeassistant/sensor/hapanel_ota_target_slot/config
homeassistant/sensor/hapanel_last_command_result/config
homeassistant/binary_sensor/hapanel_psram_ready/config
homeassistant/button/hapanel_status_refresh/config
homeassistant/button/hapanel_ui_refresh/config
homeassistant/button/hapanel_ota_preflight/config
homeassistant/button/hapanel_ota_reboot/config
```

The app-version entity reads from `CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC`.
The uptime, Wi-Fi status, MQTT status, OTA status, OTA readiness, OTA slot, and
PSRAM readiness entities read from `CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`.
Wi-Fi, MQTT, and OTA are also exposed as top-level `wifi`, `mqtt`, and `ota`
objects in the retained state payload so discovery templates do not depend on
service-array ordering. The top-level `ota.preflight` object reports whether
the OTA gate is open, its reason, the running slot, and the next target slot.
The top-level `ota.inventory` object reports the running, boot, factory,
`ota_0`, and `ota_1` partitions, boot/running agreement, rollback support, and
running image state. It also reports a normalized `phase` and
`reboot_required` flag so a staged image is visible without comparing partition
objects manually. The top-level `ota.progress` object reports active OTA
transport/write progress, including phase, target slot, byte counts, and
percent complete.
All discovered entities use
`CONFIG_HAPANEL_MQTT_AVAILABILITY_TOPIC` for online/offline availability and
group under the HAPanel device in Home Assistant.

The discovered buttons publish non-retained command payloads to
`CONFIG_HAPANEL_MQTT_COMMAND_TOPIC`:

```json
{"command":"status_refresh"}
{"command":"ui_refresh"}
{"command":"ota_preflight"}
{"command":"ota_reboot"}
```

`ota_preflight` does not write flash or reboot the panel. It only calls the OTA
preflight gate, refreshes retained device state, and publishes the result to the
command result/state topics.

`ota_update` is an explicit admin command:

```json
{"command":"ota_update","url":"http://192.168.42.22:8000/hapanel.bin"}
```

It requires a plain HTTP URL that returns a known `Content-Length`. The panel
downloads the image on a worker task, writes only the inactive OTA slot, validates
the image, sets the next boot partition, reports `Reboot needed`, and leaves the
actual restart to a separate manual action.

`ota_reboot` is the guarded restart command for that separate manual action. It
is rejected unless a staged OTA image is already selected as the next boot
target.

`ota_self_test_stage` is development-only. When enabled, it copies the running
app image into the inactive OTA slot through the OTA install session and sets
that slot as the next boot target. It writes flash and changes OTA boot
metadata, but it does not reboot automatically.

The last-command-result sensor reads from
`CONFIG_HAPANEL_MQTT_COMMAND_STATE_TOPIC`. The event-style result topic remains
non-retained for command round-trip listeners, while the state topic is retained
so Home Assistant can recover the last command result after reconnecting.

Firmware version values come from the ESP-IDF application descriptor. The root
CMake project sets that descriptor to `0.1.0+<git-describe>` when Git metadata
is available, with `-dirty` appended for uncommitted builds.

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
.\tools\mqtt_command_test.ps1 -Command ota_preflight
.\tools\mqtt_command_test.ps1 -Command ota_self_test_stage -ExpectStatus rejected
.\tools\mqtt_command_test.ps1 -Command unknown_command -ExpectStatus rejected
```

For OTA update testing, publish JSON directly until the helper grows arbitrary
payload support:

```json
{"command":"ota_update","url":"http://192.168.42.22:8000/hapanel.bin"}
```

From a Windows development machine, `tools/serve_static_file.ps1` can serve the
built firmware with a fixed `Content-Length`:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\serve_static_file.ps1 -FilePath .\build\hapanel.bin -Port 8000
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
