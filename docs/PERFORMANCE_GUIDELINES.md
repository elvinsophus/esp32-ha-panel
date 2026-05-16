# Performance Guidelines

Target FPS:
- 60 preferred
- 30 minimum

Cold boot target:
- under 5 seconds preferred

UI response latency:
- under 100ms preferred

## Embedded Constraints

- RAM is limited
- avoid fragmentation
- avoid unnecessary redraws
- prefer event-driven architecture
- avoid blocking UI tasks

Animation quality should adapt dynamically to hardware capability.
