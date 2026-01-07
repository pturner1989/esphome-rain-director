# Project Guidelines

This file defines project-specific conventions that SDD agents must follow during the design phase.

---

## Referenced Documentation

- README.md - Project overview and installation instructions
- ESPHome documentation: https://esphome.io

---

## Error Handling

This is an ESPHome custom component project. Error handling follows ESPHome conventions:

- Use ESP_LOGW for warnings (e.g., unknown protocol codes)
- Use ESP_LOGE for errors (e.g., communication failures)
- Components should remain operational even when encountering unknown data
- Unknown hex codes should be logged but not crash the component
- Graceful degradation: show "Unknown" for unrecognized states

---

## Logging

ESPHome logging conventions:

- Use ESP_LOGD for debug messages (verbose, only shown when debug enabled)
- Use ESP_LOGI for informational messages (normal operation)
- Use ESP_LOGW for warnings (unexpected but handled conditions)
- Use ESP_LOGE for errors (failures that affect functionality)
- Use ESP_LOGV for very verbose debug output
- Include component name in log context (handled by ESPHome macros)

---

## Naming Conventions

ESPHome custom component conventions:

- Component folder: `components/rain_director/` (snake_case)
- Python config: `__init__.py` for configuration schema
- C++ files: `rain_director.h`, `rain_director.cpp` (snake_case)
- C++ classes: `RainDirector` (PascalCase)
- C++ methods: `get_tank_level()` (snake_case)
- YAML configuration keys: `rain_director:`, `tank_capacity:` (snake_case)
- Entity IDs in Home Assistant: `sensor.rain_director_tank_level` (snake_case with dots)

---

## Testing Conventions

This project does not have automated tests. Validation is manual:

- Test by building and flashing to actual ESP32 hardware
- Verify sensors appear correctly in Home Assistant
- Test error conditions by disconnecting Rain Director
- Verify WiFi reconnection behavior

Note: ESPHome custom components typically don't have unit tests due to hardware dependencies.

---

## Additional Guidelines

### ESPHome Architecture
- Custom components extend ESPHome's Component class
- UART communication uses ESPHome's UARTDevice mixin
- Sensors use ESPHome's sensor::Sensor class
- Text sensors use ESPHome's text_sensor::TextSensor class
- Configuration validation happens in Python (`__init__.py`)
- Component logic is in C++ (`.h` and `.cpp` files)

### File Structure
- User-facing example: `install.yaml` (minimal config for users)
- Main package config: `rain-director.yaml` (imported via packages)
- Custom component: `components/rain_director/`

### Documentation
- Single README.md with all user documentation
- Keep instructions concise for intermediate ESPHome users
- One installation method only (packages import from GitHub)

---

## Notes for Agents

When reading this file:
1. Follow ESPHome conventions for custom component development
2. Use ESPHome logging macros, not custom logging
3. Keep the component simple - this is a single-purpose monitoring tool
4. README should be the only user documentation
5. No unit tests - validation is via manual hardware testing
