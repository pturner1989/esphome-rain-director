# Specification: Initial Release

**Version:** 1.1
**Date:** 2026-01-07
**Status:** Approved

---

## Problem Statement

The esphome-rain-director repository was created from a prototype project and currently presents several usability issues for new users:

1. **Complex installation process** - Multiple installation methods and unclear file structure make it difficult for users to understand which approach to use and which files to install
2. **Confusing repository structure** - File naming (rain-director.yaml vs rain-director-install.yaml) doesn't clearly communicate purpose or recommended usage
3. **Uncertain packaging configuration** - It's unclear whether the ESPHome packages feature is correctly configured to fetch both the main configuration and custom component from GitHub
4. **Difficult custom component maintenance** - Hex-to-mode code mappings are scattered or poorly structured in C++, making it difficult for contributors to submit PRs with newly discovered codes

These issues prevent intermediate ESPHome users who own a Rain Director controller from achieving self-service installation and setup. Users should be able to get the system working without opening issues or asking questions, but the current complexity creates friction.

## Beneficiaries

**Primary:**
- Home Assistant users with intermediate ESPHome experience who own a Rain Director rainwater controller and want to monitor it through their home automation system

**Secondary:**
- Contributors who discover new Rain Director protocol codes and want to easily submit PR mappings
- Future users who will benefit from clearer documentation and simpler setup process

---

## Functional Requirements

### REQ-FN-01: Simplified Installation Method
**Description:** The repository must provide a single, clear installation method using ESPHome's packages feature to import configuration from GitHub. Users create a minimal YAML file that references the remote package, eliminating confusion about which files to use.

**Examples:**
- Positive case: User creates a file with WiFi credentials and package import, runs ESPHome build, and firmware is compiled with all necessary components fetched from GitHub
- Edge case: User has no internet connectivity during build - ESPHome should fail with clear error message indicating network access is required for initial package fetch

### REQ-FN-02: Remote Component Source Configuration
**Description:** The ESPHome packages feature must be correctly configured to fetch both the main YAML configuration AND the custom rain_director component from the GitHub repository. The external_components section in rain-director.yaml must use `github://` or `type: git` source pointing to the repository, NOT `type: local` with a local path.

**Acceptance Criteria:**
- The external_components section in rain-director.yaml must use `github://` or `type: git` source, NOT `type: local`
- Users building firmware for the first time can successfully fetch both rain-director.yaml and the custom component from GitHub
- Compilation succeeds without requiring local files

**Examples:**
- Positive case: User builds firmware for the first time, ESPHome fetches rain-director.yaml and components/rain_director/ from GitHub, compilation succeeds
- Edge case: GitHub is temporarily unavailable - ESPHome should use cached version if available, or fail with clear error if not

### REQ-FN-03: Clear File Structure and Naming
**Description:** Repository file structure must clearly communicate purpose and eliminate confusion about which files are for users vs. internal project files. The example installation file must be renamed from `rain-director-install.yaml` to `install.yaml` to clearly indicate its purpose as a user-facing example.

**Acceptance Criteria:**
- Rename `rain-director-install.yaml` to `install.yaml`
- File naming makes it obvious that install.yaml is the example to copy and rain-director.yaml is the main config (fetched automatically)

**Examples:**
- Positive case: User browses repository and immediately understands that install.yaml is the example to follow, and rain-director.yaml is the main config (fetched automatically)
- Edge case: Advanced user wants to clone repo and modify locally - file structure should still make sense for this use case

### REQ-FN-04: Maintainable Code Mappings
**Description:** Hex-to-mode and hex-to-state code mappings must be clearly structured in C++ to make it easy for contributors to submit PRs with newly discovered codes. Mappings should be consolidated in a single, well-documented location at the top of the implementation file as constant arrays with clear comments explaining the format.

**Acceptance Criteria:**
- All hex-to-mode and hex-to-state mappings defined in a single constant array or structure at the top of the .cpp file
- Clear comments document the mapping format and provide examples
- Structure makes it obvious where to add new codes (e.g., clearly labeled section with pattern to follow)

**Examples:**
- Positive case: Contributor discovers new mode code 0x14, opens rain_director.cpp, sees clearly labeled MODE_MAPPINGS array at top with format examples, adds one line with new code, submits PR
- Edge case: Contributor adds duplicate or conflicting mapping - code review catches this due to clear structure and visibility

### REQ-FN-05: Complete User Documentation
**Description:** README must provide all necessary information for self-service installation including hardware requirements with warnings, complete wiring instructions, single installation method, troubleshooting guidance, and WiFi fallback notes. Documentation must address current gaps: missing troubleshooting section and overly complex installation options.

**Acceptance Criteria:**
- Hardware section includes specific warning about MAX485 auto-direction requirement
- Complete wiring instructions with RJ45 pinout, power, and RS-485 connections
- Single installation method shown (packages import) - remove other options from main instructions
- New troubleshooting section added covering: no data (try restarting Rain Director, swapping RJ45 ports), garbled data (swap A/B wires), WiFi issues
- Note about fallback WiFi AP mode if credentials are incorrect

**Examples:**
- Positive case: User reads README, purchases correct MAX485 module with auto-direction, follows wiring instructions, uses single install method, system works without questions
- Edge case: User gets no data, consults troubleshooting section, restarts Rain Director or swaps ports, problem resolved without opening issue

### REQ-FN-06: Minimal User Configuration
**Description:** Users must only need to configure device name, tank capacity, and WiFi credentials in their YAML file. All other configuration is provided by the remote package.

**Acceptance Criteria:**
- User's local YAML only requires: device name (substitution), tank_capacity (substitution), and WiFi ssid/password (YAML config or secrets)
- All other configuration (GPIO pins, baud rate, sensors, etc.) provided by imported package

**Examples:**
- Positive case: User copies install.yaml example, changes device name, tank capacity, and WiFi credentials, builds and flashes successfully
- Edge case: User wants to customize GPIO pins or baud rate - they can override package values in their local YAML (standard ESPHome behavior)

### REQ-FN-07: Data Visibility in Home Assistant
**Description:** Once installed and connected, the following sensors must appear in Home Assistant with valid data: Tank Level (%), Tank Volume (L), Mode (text), Status (text), Source (text), Rainwater Used (L), Mains Used (L).

**Examples:**
- Positive case: User adds device to Home Assistant, all seven sensors show current values within 30 seconds
- Edge case: Rain Director is in an unusual state (e.g., error mode) - sensors should show "Unknown" or last known value rather than crashing

---

## Non-Functional Requirements

### REQ-NFN-01: Data Publication Latency
**Category:** Performance
**Description:** Data must be published to Home Assistant immediately (within 1 second) after being received from the Rain Director UART. Note that overall latency from Rain Director state change to Home Assistant display depends on the Rain Director's transmission interval, which is controlled by the Rain Director firmware, not this component.

**Acceptance Threshold:** Tank level changes are published to Home Assistant within 1 second of UART reception under normal WiFi conditions; component introduces minimal latency beyond Rain Director's transmission schedule

### REQ-NFN-02: Standard ESPHome WiFi Behavior
**Category:** Reliability
**Description:** Configuration must use standard ESPHome WiFi component with default automatic reconnection behavior. ESP32 automatically reconnects to WiFi if connection is lost and gracefully handles Rain Director power cycles (which also power cycle the ESP32).

**Acceptance Threshold:** Uses standard ESPHome wifi component configuration; automatic WiFi reconnection works without custom code; ESP32 resumes operation after power cycle without manual intervention

### REQ-NFN-03: ESPHome Version Compatibility
**Category:** Compatibility
**Description:** Configuration must work with recent ESPHome versions. Currently tested and verified with ESPHome 2025.12.1.

**Acceptance Threshold:** Successfully builds and operates with ESPHome 2025.12.1; no specific minimum version requirement but should use stable/standard ESPHome features to maximize compatibility

### REQ-NFN-04: Hardware Compatibility
**Category:** Compatibility
**Description:** Firmware must work reliably with ESP32 DevKit V1 boards. Other ESP32 variants are untested and not officially supported for this release.

**Acceptance Threshold:** Reliable operation on ESP32 DevKit V1 with esp32dev board configuration; documentation explicitly states testing is limited to this board type

### REQ-NFN-05: Memory and Resource Usage
**Category:** Performance
**Description:** Custom component and configuration must operate within ESP32 memory constraints without crashes, watchdog resets, or performance degradation.

**Acceptance Threshold:** No memory-related crashes during normal operation; component processes UART data without dropping messages; WiFi and API remain responsive

### REQ-NFN-06: Package Refresh Strategy
**Category:** Maintainability
**Description:** Remote package configuration must include a refresh interval so users automatically receive component improvements and bug fixes when they rebuild firmware.

**Acceptance Threshold:** Package configuration includes "refresh: 1d" (daily check for updates); documentation explains that rebuilding firmware pulls latest version from GitHub

### REQ-NFN-07: Graceful Degradation
**Category:** Reliability
**Description:** Component must handle incomplete or unexpected data from Rain Director without crashing, logging errors appropriately and maintaining last known good state.

**Acceptance Threshold:** Unknown/invalid codes result in "Unknown" state or retention of previous value; errors logged at appropriate level (WARN for unknown codes, ERROR for critical issues); component remains operational

___

## Explicitly Out of Scope

- Support for other Rain Director models, variants, or manufacturers (only the UK Rainwater Harvesting Rain Director)
- Integration with home automation platforms other than Home Assistant (no MQTT, no standalone operation)
- Advanced features such as historical graphs, water usage predictions, or automation examples
- Mobile app or custom user interfaces beyond standard Home Assistant entity cards
- Multi-language documentation (English only)
- Video tutorials, photos, or diagrams of hardware wiring
- Support for connecting multiple Rain Director units to a single ESP32
- Reverse engineering additional protocol details beyond current known codes
- Validation or testing with MAX485 modules that require DE/RE control pins
- Support for Rain Director firmware versions with incompatible protocols (assume current protocol is stable)
- Over-the-air (OTA) updates without Home Assistant (standard ESPHome OTA via HA API only)

---

## Open Questions

None - all requirements have been clarified through stakeholder interview.

---

## Appendix

### Assumptions and Dependencies

**ESPHome Platform Assumptions:**
- Standard ESPHome WiFi component provides automatic reconnection without custom implementation
- Standard ESPHome OTA component provides firmware updates via Home Assistant API
- ESPHome packages feature correctly fetches both YAML and external_components from public GitHub repositories
- Users have intermediate ESPHome experience and understand basic YAML configuration

**Hardware Dependencies:**
- ESP32 powered by Rain Director's 12V supply (via buck converter) - ESP32 loses power when Rain Director is unplugged
- MAX485 module must be auto-direction type (no DE/RE control required)
- Rain Director uses consistent UART protocol at 9600 baud
- Rain Director RJ45 pinout remains stable across units

**Network Dependencies:**
- Internet connectivity required during initial firmware build to fetch packages from GitHub
- GitHub repository must remain public for packages feature to work
- WiFi network required for Home Assistant integration

### Glossary
- **Rain Director:** A rainwater tank controller manufactured by Rainwater Harvesting (UK) that manages switching between rainwater and mains water supply
- **ESPHome:** An open-source system for controlling ESP8266/ESP32 microcontrollers with simple YAML configuration, designed for Home Assistant integration
- **UART:** Universal Asynchronous Receiver-Transmitter - serial communication protocol used by Rain Director (9600 baud)
- **RS-485:** Industrial serial communication standard used by Rain Director, requires MAX485 converter to interface with ESP32 TTL logic
- **MAX485 Module:** RS-485 to TTL serial converter chip/module; "auto-direction" type automatically switches between transmit and receive without DE/RE control pins
- **Packages Feature:** ESPHome functionality allowing YAML configurations to import/inherit from remote sources (GitHub, local files)
- **External Components:** ESPHome mechanism for including custom C++ components not in the core ESPHome distribution
- **Buck Converter:** DC-DC step-down voltage converter (12V to 5V in this project) to power ESP32 from Rain Director's 12V supply
- **Header Tank:** Small water tank (typically in loft/attic) that the Rain Director keeps filled for household use
- **Home Assistant:** Open-source home automation platform that ESPHome is designed to integrate with

### References
- ESPHome Documentation: https://esphome.io
- ESPHome Packages Documentation: https://esphome.io/guides/configuration-types.html#packages
- ESPHome External Components: https://esphome.io/components/external_components.html
- Project Repository: https://github.com/pturner1989/esphome-rain-director
- Rain Director Manufacturer: Rainwater Harvesting Ltd (UK)

### Change History
| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-07 | Claude (Technical Analyst) | Initial specification based on stakeholder interview |
| 1.1 | 2026-01-07 | Claude (Technical Analyst) | Updated per review feedback: reframed code mappings (REQ-FN-04), added acceptance criteria to REQ-FN-02, specified file rename in REQ-FN-03, consolidated documentation requirements (REQ-FN-05), fixed REQ-FN-06 wording, recategorized WiFi reconnection as REQ-NFN-02, clarified data latency in REQ-NFN-01, removed redundant NFN-07 |
