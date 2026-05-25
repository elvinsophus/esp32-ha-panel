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
- includes the requested/rendered UI page and current layer in retained device
  state
- publishes retained Home Assistant MQTT discovery for early diagnostic
  entities when `CONFIG_HAPANEL_MQTT_HA_DISCOVERY_ENABLE` is enabled
- publishes retained Home Assistant MQTT discovery for diagnostic command
  buttons that use the existing safe command topic
- publishes retained Home Assistant MQTT discovery for a last-command-result
  diagnostic sensor backed by the retained command state topic
- publishes retained Home Assistant MQTT discovery for last-home-action and
  last-home-command diagnostic sensors backed by retained Home topics
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
- supports `{"command":"ui_show_root"}`, `{"command":"ui_show_home"}`,
  `{"command":"ui_show_status"}`, `{"command":"ui_show_security"}`, and
  `{"command":"ui_show_apps"}` for early page-router validation
- subscribes to three configurable Home page tile state topics:
  `CONFIG_HAPANEL_MQTT_HOME_SCENE_TOPIC`,
  `CONFIG_HAPANEL_MQTT_HOME_LIGHTS_TOPIC`, and
  `CONFIG_HAPANEL_MQTT_HOME_CLIMATE_TOPIC`
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
homeassistant/sensor/hapanel_last_home_action/config
homeassistant/sensor/hapanel_last_home_command/config
homeassistant/binary_sensor/hapanel_psram_ready/config
homeassistant/button/hapanel_status_refresh/config
homeassistant/button/hapanel_ui_refresh/config
homeassistant/button/hapanel_ui_show_home/config
homeassistant/button/hapanel_ui_show_status/config
homeassistant/button/hapanel_ui_show_security/config
homeassistant/button/hapanel_ui_show_apps/config
homeassistant/button/hapanel_ota_preflight/config
homeassistant/button/hapanel_ota_reboot/config
```

The app-version entity reads from `CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC`.
The uptime, Wi-Fi status, MQTT status, OTA status, OTA readiness, OTA slot, and
PSRAM readiness entities read from `CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`.
The top-level `ui` object reports the requested page, rendered page, and layer
for page-router bring-up. The top-level `home` object reports the current Home
tile labels, values, online flags, and revisions.
Home detail row taps publish non-retained action events to
`CONFIG_HAPANEL_MQTT_HOME_ACTION_TOPIC` and retain the latest action on
`CONFIG_HAPANEL_MQTT_HOME_ACTION_STATE_TOPIC` for diagnostics.
If the tapped row includes an action target, the panel also publishes a
non-retained command request to `CONFIG_HAPANEL_MQTT_HOME_COMMAND_TOPIC` for
Home Assistant automations to execute. The latest request is also retained on
`CONFIG_HAPANEL_MQTT_HOME_COMMAND_STATE_TOPIC` for diagnostics.
Home Assistant discovery exposes both retained payloads as diagnostic sensors:
`Last Home Action` and `Last Home Command`.
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
{"command":"ui_show_root"}
{"command":"ui_show_home"}
{"command":"ui_show_status"}
{"command":"ui_show_security"}
{"command":"ui_show_apps"}
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

For OTA update testing, use the helper's `-Payload` option when you need fields
beyond the command name:

```powershell
$payload = @{id="manual-ota-1"; command="ota_update"; url="http://192.168.42.22:8000/hapanel.bin"} | ConvertTo-Json -Compress
.\tools\mqtt_command_test.ps1 -Id manual-ota-1 -Payload $payload -ExpectStatus accepted
```

From a Windows development machine, `tools/serve_static_file.ps1` can serve the
built firmware with a fixed `Content-Length`:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\serve_static_file.ps1 -FilePath .\build\hapanel.bin -Port 8000
```

To run the complete OTA loop from the development workstation, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ota_roundtrip_test.ps1 -HostIp 192.168.42.40
```

The round-trip helper serves `build\hapanel.bin`, sends `ota_update`, waits for
`ota.progress.phase=staged` and `ota.inventory.reboot_required=true`, sends the
guarded `ota_reboot` command, and verifies that the panel comes back running
from the staged slot. Pass `-NoReboot` to stop after staging.

The MQTT helper scripts read broker, credential, command topic, and result topic
settings from `sdkconfig` and `sdkconfig.defaults.local`. They print JSON result
payloads and exit with an error if the expected result does not arrive.

## Topic Dump

Use `tools/mqtt_topic_dump.ps1` to inspect retained state or discovery topics:

```powershell
.\tools\mqtt_topic_dump.ps1 -Json
.\tools\mqtt_topic_dump.ps1 -Topic homeassistant/sensor/hapanel_ota_status/config -Json
```

The script reads broker and credential settings from `sdkconfig` and
`sdkconfig.defaults.local`. Without `-Topic`, it reads
`CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC`.

## Home Page Tile Topics

The Home page listens to compact retained text payloads on three configurable
topics:

```text
CONFIG_HAPANEL_MQTT_HOME_SCENE_TOPIC    default hapanel/home/scene
CONFIG_HAPANEL_MQTT_HOME_LIGHTS_TOPIC   default hapanel/home/lights
CONFIG_HAPANEL_MQTT_HOME_CLIMATE_TOPIC  default hapanel/home/climate
CONFIG_HAPANEL_MQTT_HOME_ACTION_TOPIC   default hapanel/home/action
CONFIG_HAPANEL_MQTT_HOME_ACTION_STATE_TOPIC default hapanel/home/action/state
CONFIG_HAPANEL_MQTT_HOME_COMMAND_TOPIC  default hapanel/home/command
CONFIG_HAPANEL_MQTT_HOME_COMMAND_STATE_TOPIC default hapanel/home/command/state
```

The first line becomes the matching Home tile summary. Optional following lines
become detail rows on that category page. Detail rows accept either
`Label: Value` or `Label=Value`; rows without a separator are displayed as
numbered details. Empty, `unknown`, `unavailable`, and `offline` summaries mark
the category offline; any other summary marks it online.

Rows can also include optional action metadata:

```text
Kitchen: On | light.kitchen | toggle
Dining: Off | light.dining
```

The first metadata field is the target. The second field is the requested
action and defaults to `toggle` when omitted. The panel does not execute Home
Assistant services directly; it only publishes a structured request for HA
automations or scripts to handle.

This stays deliberately small: the firmware keeps a fixed number of detail
rows per category and does not parse a large Home Assistant JSON object on the
panel.

When a detail row is tapped, the panel publishes a non-retained event:

```json
{"schema":"hapanel.home_action.v1","entity":"lights","category":"Lights","detail_index":0,"label":"Kitchen","value":"On","target":"light.kitchen","action":"toggle","online":true}
```

The same payload is retained on `hapanel/home/action/state` as the latest Home
action. Home Assistant discovery exposes this as the `Last Home Action`
diagnostic sensor.

Rows with a non-empty target additionally publish a non-retained command request:

```json
{"schema":"hapanel.home_command.v1","entity":"lights","category":"Lights","detail_index":0,"label":"Kitchen","target":"light.kitchen","action":"toggle"}
```

The same command request payload is retained on
`hapanel/home/command/state` for verification and diagnostics.

Manual publish example:

```powershell
.\tools\mqtt_publish.ps1 -Topic hapanel/home/scene -Payload "Dinner" -Retain
.\tools\mqtt_publish.ps1 -Topic hapanel/home/lights -Payload "Kitchen on" -Retain
.\tools\mqtt_publish.ps1 -Topic hapanel/home/climate -Payload "22.5 C" -Retain
```

Multi-line detail example:

```text
Kitchen on
Kitchen: On | light.kitchen
Dining: Off | light.dining
Hall: Off | light.hall
```
