# Specification: Init and Backup Modes

**Version:** 1.1
**Date:** 2026-01-07
**Status:** Approved

---

## Problem Statement

The Rain Director custom component currently displays "Unknown" for several operational states that occur during system initialization and mains backup scenarios. Users see these unknown states in Home Assistant and cannot understand what the Rain Director is doing during these critical operations. Additionally, the current implementation uses only the mode byte for state determination, which is insufficient to distinguish between similar operations that differ only in their status byte (e.g., initialization filling vs normal filling both use mode 0x40). This creates ambiguity and prevents accurate state reporting.

## Beneficiaries

**Primary:**
- Home Assistant users monitoring Rain Director systems who want clear visibility into initialization sequences and mains backup operations

**Secondary:**
- Users troubleshooting Rain Director behavior who need diagnostic hex codes to understand raw protocol states
- Future contributors who will benefit from a more robust state mapping architecture that uses composite keys

---

## Functional Requirements

### REQ-FN-01: Composite Key Mode Mapping
**Description:** The component must use both the mode byte (from display panel hex codes) AND the status byte (from JSON messages) as a composite key to determine the correct Mode, Status, and Source strings. The MODE_MAPPINGS data structure must be updated to include status_byte as part of the matching criteria, enabling disambiguation of states that share the same mode byte but have different status bytes. When a mode byte is received before a status byte is available, the component must use the last known status byte value for the composite lookup to ensure continuous state reporting.

**Matching Priority:**
The lookup algorithm must check mappings in this order:
1. Status-specific mappings (exact mode + status match) - checked first
2. Status-agnostic mappings (mode match, any status) - fallback for modes that don't require status disambiguation

**Acceptance Criteria:**
- Component maintains last known status byte value in member variable
- When mode byte arrives, composite lookup uses current or last known status byte
- Status-specific mappings take precedence over status-agnostic mappings
- Mode 0x00 with status 0x01 maps to Backup (status-specific), otherwise maps to Normal (fallback)

**Examples:**
- Positive case: Mode 0x40 with Status 0x0F maps to "Init/Filling/Rainwater" while Mode 0x40 with Status 0x09 maps to "Init/Filling/Mains"
- Positive case: Mode 0x00 with Status 0x01 maps to "Backup/Filling/Mains" (status-specific match) while Mode 0x00 with Status 0x00 maps to "Normal/Filling/Rainwater" (fallback match)
- Edge case: Mode byte 0x02 received before any status byte - component uses initialized default status (e.g., 0x00) for lookup until first status byte arrives
- Edge case: Mode byte changes from 0x01 to 0x40, status byte remains 0x0F - component correctly updates from "Normal/Idle" to "Init/Filling/Rainwater"

### REQ-FN-02: New Initialization Mode Mappings
**Description:** The component must recognize and correctly map three new initialization states that occur during Rain Director boot-up sequence. These states provide visibility into the startup process where the Rain Director drains and refills the header tank.

**Mappings:**
- Mode 0xC0 (192) + Status 0x0F (15) → Mode: "Init", Status: "Draining", Source: "Rainwater"
- Mode 0x40 (64) + Status 0x0F (15) → Mode: "Init", Status: "Filling", Source: "Rainwater"
- Mode 0x40 (64) + Status 0x09 (9) → Mode: "Init", Status: "Filling", Source: "Mains"

**Examples:**
- Positive case: Rain Director boots up, sends 0xC0/0x0F, Home Assistant displays "Init" mode with "Draining" status
- Positive case: Initialization completes draining, sends 0x40/0x0F, Home Assistant updates to "Init" mode with "Filling" status and "Rainwater" source
- Edge case: User restarts Rain Director while monitoring - initialization sequence is displayed correctly in sequence (Draining → Refilling Rainwater → Refilling Mains)

### REQ-FN-03: New Mains Backup Mode Mappings
**Description:** The component must recognize and correctly map two new mains backup states that occur when the rainwater tank is empty and the system operates on mains water only. These states provide visibility into backup operation mode.

**Mappings:**
- Mode 0x02 (2) + Status 0x01 (1) → Mode: "Backup", Status: "Idle", Source: "Mains"
- Mode 0x00 (0) + Status 0x01 (1) → Mode: "Backup", Status: "Filling", Source: "Mains"

**Examples:**
- Positive case: Rainwater tank is empty, Rain Director sends 0x02/0x01, Home Assistant displays "Backup" mode with "Idle" status and "Mains" source
- Positive case: Backup mode triggers fill, sends 0x00/0x01, Home Assistant updates to "Backup" mode with "Filling" status
- Edge case: Rainwater becomes available during backup mode - system transitions from Backup mode to Normal mode correctly

### REQ-FN-04: Hexadecimal Mode Code Display
**Description:** The mode_code sensor must be changed from a numeric sensor (publishing integer values) to a text sensor (publishing hexadecimal string values in uppercase format "0xXX"). This change provides better alignment with protocol documentation and makes it easier for users to correlate diagnostic values with protocol specifications. This is a breaking change that will create a new entity in Home Assistant - the old numeric mode_code sensor will become unavailable and a new text sensor will appear.

**Format Specification:**
- Hexadecimal format with "0x" prefix
- Two uppercase hex digits (e.g., "0xC0", "0x00", "0x0F")
- Leading zero for single-digit hex values (e.g., "0x09" not "0x9")

**User Impact:**
- Existing mode_code sensor (numeric) will become unavailable in Home Assistant
- New mode_code sensor (text) will appear with hexadecimal values
- Users with dashboards or automations referencing the old numeric sensor may need to update their configurations
- Historical numeric data from the old sensor will remain but will not continue to populate

**Examples:**
- Positive case: Mode byte 192 is displayed as "0xC0" in Home Assistant (not "192", not "0xc0")
- Positive case: Mode byte 0 is displayed as "0x00" in Home Assistant (not "0", not "0x0")
- Positive case: Mode byte 15 is displayed as "0x0F" in Home Assistant (not "15", not "0xf")
- Edge case: Unknown mode byte 255 is displayed as "0xFF" and Mode/Status/Source show "Unknown"

### REQ-FN-05: Remove State Code Sensor
**Description:** The state_code diagnostic sensor must be removed from the component configuration and implementation. The status byte will be used internally for composite key mapping but will not be exposed as a separate diagnostic sensor. This simplifies the diagnostic interface and eliminates confusion about which code represents which aspect of operation.

**Examples:**
- Positive case: After implementation, Home Assistant shows only mode_code (hex format), Mode, Status, and Source sensors - no state_code sensor
- Negative case: state_code sensor does NOT appear in Home Assistant entity list
- Edge case: Existing state_code sensor becomes unavailable after upgrade, does not cause errors

### REQ-FN-06: Existing Mode Compatibility
**Description:** All existing mode mappings must continue to function correctly with the new composite key architecture. The implementation must distinguish between two types of mappings: truly status-agnostic modes that match regardless of status byte value, and modes that have status-specific mappings with status-agnostic fallback behavior.

**Status-Agnostic Modes (match any status byte):**
- Mode 0x01 → "Normal/Idle/Rainwater"
- Mode 0x04 → "Normal/Idle/Mains"
- Mode 0x08 → "Holiday/Idle/Mains"
- Mode 0x0C → "Holiday/Filling/Mains"
- Mode 0x10 → "Refresh/Draining/Rainwater" (with is_refresh flag)

**Status-Specific with Fallback:**
- Mode 0x00:
  - If status = 0x01: "Backup/Filling/Mains" (status-specific mapping)
  - Otherwise: "Normal/Filling/Rainwater" (fallback mapping)
- Mode 0x40:
  - If status = 0x0F: "Init/Filling/Rainwater" (status-specific mapping)
  - If status = 0x09: "Init/Filling/Mains" (status-specific mapping)
  - Otherwise: behavior undefined (may map to Unknown or require fallback)
- Mode 0x02:
  - If status = 0x01: "Backup/Idle/Mains" (status-specific mapping)
  - Otherwise: behavior undefined (may map to Unknown)
- Mode 0xC0:
  - If status = 0x0F: "Init/Draining/Rainwater" (status-specific mapping)
  - Otherwise: behavior undefined (may map to Unknown)

**Examples:**
- Positive case: Rain Director sends mode 0x01 with any status value, system displays "Normal/Idle/Rainwater"
- Positive case: Mode 0x00 with status 0x01 maps to "Backup/Filling/Mains" (status-specific match takes priority)
- Positive case: Mode 0x00 with status 0x00 maps to "Normal/Filling/Rainwater" (fallback when status doesn't match specific mapping)
- Edge case: Mode 0x40 with unknown status byte 0x05 - behavior depends on implementation (may show Unknown or use fallback if defined)

---

## Non-Functional Requirements

### REQ-NFN-01: State Update Latency
**Category:** Performance
**Description:** State updates using the new composite key lookup must occur within the same latency requirements as the existing single-key lookup. The addition of status byte matching must not introduce measurable delay in state publication to Home Assistant.

**Acceptance Threshold:** State changes published to Home Assistant within 1 second of receiving both mode and status bytes from UART; composite key lookup adds less than 10ms overhead vs single key lookup

### REQ-NFN-02: Data Structure Maintainability
**Category:** Maintainability
**Description:** The updated MODE_MAPPINGS data structure must remain simple to understand and modify for contributors adding new discovered codes, despite the added complexity of composite key matching. The composite key approach must be clearly documented with examples showing how to add entries that require status byte matching vs entries that are status-agnostic. Comments must acknowledge that the composite key structure is inherently more complex than the previous single-key approach but should minimize this complexity through clear organization.

**Acceptance Threshold:** MODE_MAPPINGS structure includes clear comments explaining composite key format and matching priority; example entries demonstrate both status-specific and status-agnostic patterns; comments explain when to use each pattern; format follows existing maintainability standards from initial-release specification REQ-FN-04

### REQ-NFN-03: Backward Compatibility
**Category:** Compatibility
**Description:** Users upgrading from previous component versions must experience graceful transition to the new composite key system without loss of core functionality. Existing mode mappings must continue to work without requiring changes to user YAML configurations. However, users should expect the mode_code sensor to change type (numeric to text), which may require manual updates to dashboards or automations.

**Acceptance Threshold:** Existing users can update component via package refresh; no YAML configuration changes required; all previously working Mode/Status/Source text sensors continue to report correctly; new states appear automatically without user intervention; mode_code sensor transitions from numeric to text (breaking change acknowledged)

### REQ-NFN-04: Unknown State Handling
**Category:** Reliability
**Description:** The component must gracefully handle mode/status combinations that are not defined in MODE_MAPPINGS without crashing. Unknown combinations should result in "Unknown" being published to Mode, Status, and Source text sensors, with mode_code showing the hex value for diagnostic purposes.

**Acceptance Threshold:** Unknown mode/status combinations log warning with both mode and status hex values; text sensors publish "Unknown"; component continues operating; mode_code sensor shows the mode byte in hex format for debugging

___

## Explicitly Out of Scope

- Reverse engineering additional Rain Director protocol codes beyond the 5 new codes provided
- Creating separate initialization or backup mode sensors (all states use existing Mode/Status/Source sensors)
- Exposing status byte as a separate diagnostic sensor (removed per REQ-FN-05)
- Automatic detection or learning of new mode/status combinations (manual code mapping required)
- Migration tools or warnings for users who previously interpreted state_code sensor values
- Migration path for users with historical data or automations referencing the old numeric mode_code sensor (users must manually update dashboards/automations to use new text sensor)
- Historical tracking of initialization sequences or backup mode duration
- Alerts or notifications when Rain Director enters backup mode
- Separate consumption tracking or reporting for backup mode operation (existing consumption tracking continues unchanged)
- Validation that the 5 new codes are correct or complete (assumes provided codes are accurate)
- Support for Rain Director firmware versions that may use different protocol encodings
- Documentation of the Rain Director protocol specification or boot sequence timing
- Automatic dashboard updates when mode_code sensor changes from numeric to text type

---

## Open Questions

None - all requirements have been clarified through stakeholder interview.

---

## Appendix

### Assumptions and Dependencies

**Protocol Assumptions:**
- Mode byte and status byte together uniquely identify all Rain Director operational states
- Status byte is reliably available from JSON messages at approximately the same time as mode byte from hex codes
- The 5 newly discovered mode/status combinations are correct and represent stable Rain Director behavior
- Mode bytes 0x01, 0x04, 0x08, 0x0C, 0x10 are truly status-agnostic (work with any status value)
- Mode bytes 0x00, 0x02, 0x40, 0xC0 require specific status values for correct interpretation
- Status byte value is available early enough in component initialization to avoid extended periods of using default status

**Component Architecture Assumptions:**
- The component currently receives mode byte from hex code parsing (<1053...> format)
- The component currently receives status byte from JSON parsing ({"tanklevels":{"state":"..."}} format)
- Both data sources arrive on the same UART stream with similar timing
- The composite key lookup can be implemented without significant refactoring of the loop() and process_buffer_() methods
- Changing mode_code from numeric sensor to text sensor is acceptable as a breaking change
- Component can maintain last known status byte value in a member variable for use when mode byte updates

**User Impact Assumptions:**
- Users will recognize "Init" and "Backup" as meaningful mode labels without additional explanation
- Hexadecimal mode code display (0xXX uppercase) is more useful than decimal for diagnostic purposes
- Removal of state_code sensor will not significantly impact existing user dashboards (diagnostic sensor, not primary)
- Users with automations on numeric mode_code sensor are willing to update to text sensor (small number of users affected)
- Breaking change to mode_code sensor type is acceptable given the benefit of hex display format

### Glossary

- **Mode Byte:** The single-byte hex code sent by the Rain Director display panel (device 10) in the format <1053[MODE_BYTE]...>; represents the primary operational state
- **Status Byte:** The single-byte decimal value from JSON messages in the format {"tanklevels":{"state":"[STATUS_BYTE]",...}}; provides additional state context
- **Composite Key:** A lookup key consisting of both mode byte and status byte used together to uniquely identify a Rain Director operational state
- **Status-Specific Mapping:** A MODE_MAPPINGS entry that requires a specific status byte value to match (e.g., mode 0x00 + status 0x01)
- **Status-Agnostic Mapping:** A MODE_MAPPINGS entry that matches a mode byte regardless of status byte value (e.g., mode 0x01 with any status)
- **Fallback Mapping:** A status-agnostic mapping used when no status-specific mapping matches for a given mode byte
- **Init Mode:** Initialization mode that occurs during Rain Director boot-up, involving draining and refilling the header tank to establish baseline operation
- **Backup Mode:** Operational mode where the Rain Director operates exclusively on mains water because the rainwater tank is empty
- **MODE_MAPPINGS:** C++ data structure array that maps mode/status combinations to human-readable strings (Mode, Status, Source)
- **Hex Code:** Protocol message from Rain Director in format <XXYYZZ...> where XX is device ID, YY is command, ZZ+ is data and checksum
- **JSON Message:** Protocol message from Rain Director in format {"tanklevels":{"top":"X","state":"Y"}} containing tank level and status
- **Breaking Change:** A modification that causes existing functionality to behave differently or become unavailable, requiring user intervention (e.g., mode_code changing from numeric to text sensor)

### References

- Initial Release Specification: `/home/pete/dev/esphome-rain-director/.sdd/initial-release/specification.md`
- Current MODE_MAPPINGS Implementation: `/home/pete/dev/esphome-rain-director/components/rain_director/rain_director.cpp` (lines 26-39)
- ESPHome Text Sensor Documentation: https://esphome.io/components/text_sensor/
- ESPHome Sensor Documentation: https://esphome.io/components/sensor/

### Change History
| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-07 | Claude (Technical Analyst) | Initial specification based on stakeholder interview |
| 1.1 | 2026-01-07 | Claude (Technical Analyst) | Updated per review feedback: clarified sensor type change in REQ-FN-04 with user impact notes; specified status byte timing behavior and matching priority in REQ-FN-01; distinguished status-specific vs status-agnostic matching in REQ-FN-06; specified uppercase hex format; added complexity acknowledgment to REQ-NFN-02; added out of scope items for mode_code migration and backup consumption tracking |
