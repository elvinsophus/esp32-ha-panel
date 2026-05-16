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

## Important Principle

Security mechanisms should not compromise usability.
