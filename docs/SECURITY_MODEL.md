# Security Model

Some configurations require administrator privilege.

## Future Security Features

- configurable admin password
- protected system settings
- OTA trust validation
- credential encryption

## OTA Trust Boundary

Real OTA updates must validate update provenance before any image is accepted.
The current OTA foundation does not download or write firmware; it only reports
partition readiness. Signature, version, downgrade, and transport policy should
be designed before enabling update writes.

## Local Secrets

Local Wi-Fi and MQTT credentials can be supplied through
`sdkconfig.defaults.local`. This file is intentionally ignored by Git and should
only be used for controlled hardware bring-up until runtime provisioning and
credential storage exist.

## Important Principle

Security mechanisms should not compromise usability.
