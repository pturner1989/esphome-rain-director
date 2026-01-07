# Design: Init and Backup Modes

**Version:** 1.1
**Date:** 2026-01-07
**Status:** Approved
**Linked Specification** `.sdd/init-backup-modes/specification.md`

---

# Design Document

---

## Architecture Overview

### Current Architecture Context

The Rain Director ESPHome custom component currently uses a simple single-key lookup system to map mode bytes to human-readable state information. Here's how it works today:

**Data Sources:**
- Mode byte: Extracted from hex codes `<1053[MODE_BYTE][SUB][CHECKSUM]` sent by the display panel (device 10)
- Status byte: Extracted from JSON messages `{"tanklevels":{"state":"[STATUS_BYTE]",...}}` sent by the Rain Director controller
- Both arrive on the same UART stream at 9600 baud

**Current Lookup Logic:**
- MODE_MAPPINGS is a static array of structures (lines 32-39 in rain_director.cpp)
- Each entry contains: code (uint8_t), mode (string), status (string), source (string), is_refresh (bool)
- Lookup performs linear search matching only the mode byte (lines 157-163)
- Status byte is currently published to a diagnostic sensor but not used in state determination

**Current Limitation:**
This single-key approach creates ambiguity because different operational states share the same mode byte but differ in their status byte. For example:
- Mode 0x40 with Status 0x0F = Initialization filling from rainwater
- Mode 0x40 with Status 0x09 = Initialization filling from mains
- Mode 0x00 with Status 0x01 = Backup mode filling from mains
- Mode 0x00 with Status 0x00 = Normal filling from rainwater

The current architecture cannot distinguish these scenarios, resulting in "Unknown" states being displayed to users during critical operations like system boot and mains backup.

**Component Structure:**
- Python configuration (`__init__.py`): Defines sensor schema and registration
- C++ header (`rain_director.h`): Class definition with member variables and method declarations
- C++ implementation (`rain_director.cpp`): Protocol parsing, MODE_MAPPINGS, and state publishing
- YAML configuration (`rain-director.yaml`): User-facing sensor definitions

### Proposed Architecture

**Composite Key Lookup System:**

The core architectural change is transitioning from single-key (mode byte only) to composite-key (mode byte + status byte) matching with two-tier priority:

1. **First-tier lookup:** Check for status-specific mappings (exact mode + status match)
2. **Second-tier lookup:** Fall back to status-agnostic mappings (mode match, any status)

**Data Structure Design:**

```
MODE_MAPPINGS[] = {
  // Status-specific entries (mode + status both matter)
  { mode: 0x00, status: 0x01, mode_str: "Backup", status_str: "Filling", source_str: "Mains", is_refresh: false, match_any_status: false },
  { mode: 0x02, status: 0x01, mode_str: "Backup", status_str: "Idle", source_str: "Mains", is_refresh: false, match_any_status: false },
  { mode: 0x40, status: 0x0F, mode_str: "Init", status_str: "Filling", source_str: "Rainwater", is_refresh: false, match_any_status: false },
  { mode: 0x40, status: 0x09, mode_str: "Init", status_str: "Filling", source_str: "Mains", is_refresh: false, match_any_status: false },
  { mode: 0xC0, status: 0x0F, mode_str: "Init", status_str: "Draining", source_str: "Rainwater", is_refresh: false, match_any_status: false },

  // Status-agnostic entries (mode match, status irrelevant) - fallback behavior
  { mode: 0x00, status: 0x00, mode_str: "Normal", status_str: "Filling", source_str: "Rainwater", is_refresh: false, match_any_status: true },
  { mode: 0x01, status: 0x00, mode_str: "Normal", status_str: "Idle", source_str: "Rainwater", is_refresh: false, match_any_status: true },
  { mode: 0x04, status: 0x00, mode_str: "Normal", status_str: "Idle", source_str: "Mains", is_refresh: false, match_any_status: true },
  { mode: 0x08, status: 0x00, mode_str: "Holiday", status_str: "Idle", source_str: "Mains", is_refresh: false, match_any_status: true },
  { mode: 0x0C, status: 0x00, mode_str: "Holiday", status_str: "Filling", source_str: "Mains", is_refresh: false, match_any_status: true },
  { mode: 0x10, status: 0x00, mode_str: "Refresh", status_str: "Draining", source_str: "Rainwater", is_refresh: true, match_any_status: true },
}
```

**Key Design Decisions:**

1. **Match priority implemented via search order:** Status-specific entries are placed first in the array. The lookup algorithm performs a single linear search that checks status-specific entries first, then status-agnostic entries. The first match wins.

2. **Status byte persistence:** A new member variable `last_status_byte_` maintains the most recently received status byte value. This ensures continuous state reporting even when mode bytes arrive before status bytes (addresses edge case in REQ-FN-01).

3. **Explicit match flag:** The `match_any_status` boolean field clarifies intent. When true, the status field in the mapping is ignored during lookup. When false, both mode and status must match.

4. **Backward compatibility:** Existing status-agnostic mappings (modes 0x01, 0x04, 0x08, 0x0C, 0x10) retain their current behavior by setting `match_any_status: true`.

**Lookup Algorithm Flow:**

```
process_hex_code_() receives mode byte:
  1. Use last known status byte from last_status_byte_ member variable
  2. For each entry in MODE_MAPPINGS:
     a. If entry.mode matches mode byte:
        - If entry.match_any_status == false:
            Check if entry.status matches status byte
            If yes: Use this mapping (status-specific match)
        - If entry.match_any_status == true:
            Use this mapping (status-agnostic fallback)
  3. If no match found: Use "Unknown" for all fields
  4. Publish mode_str, status_str, source_str to text sensors
  5. Publish mode byte as hex string to mode_code text sensor

process_json_() receives status byte:
  1. Store in last_status_byte_ member variable
  2. Set status_byte_received_ flag to true
  3. Do NOT publish to any sensor (state_code sensor removed per REQ-FN-05)
```

**Timing Considerations:**

The specification assumes mode and status bytes arrive with similar timing on the UART stream. However, edge cases exist where timing is unexpected:

**Edge Case 1: Mode byte arrives before first status byte**
- `last_status_byte_` initializes to 0x00 (default/unknown status)
- First status byte from JSON updates `last_status_byte_`
- Subsequent mode bytes use the most recent `last_status_byte_` value
- This ensures immediate state reporting without waiting for synchronized messages

**Edge Case 2: JSON status arrives before any mode byte (ESP32 boot)**
- Mode, Status, and Source text sensors initialize to "Unknown" during setup()
- When first JSON message arrives, status byte is stored in `last_status_byte_`
- When first hex code arrives, composite lookup uses stored status byte
- Sensors update to correct state on first mode byte received
- This handles ESP32 boot mid-stream where JSON may precede hex codes

**Edge Case 3: Initialization sequence with "Unknown" display**
- During ESPHome component initialization, sensors show "Unknown" until first messages arrive
- This brief "Unknown" state is expected and user-visible
- mode_code sensor also shows "Unknown" initially
- State self-corrects within 1-2 seconds as UART messages arrive (typical boot time)

### Technology Decisions

**1. Keep linear search instead of hash map**

**Decision:** Continue using linear array with sequential search rather than introducing a hash map or binary search.

**Justification:**
- MODE_MAPPINGS has only ~11 entries (6 existing + 5 new)
- Linear search overhead is negligible (<1μs on ESP32)
- Maintains simplicity and readability (REQ-NFN-02 maintainability)
- Avoids memory overhead of hash map on constrained ESP32
- Contributors can easily add entries by appending to array

**Alternative considered:** Hash map with composite key (mode << 8 | status). Rejected due to complexity for minimal performance gain.

**2. Text sensor for mode_code instead of numeric sensor**

**Decision:** Change mode_code from `sensor::Sensor` (numeric) to `text_sensor::TextSensor` (string) publishing hex format "0xXX".

**Justification:**
- Aligns with protocol documentation which uses hex notation
- Easier for users to correlate with specification (REQ-FN-04)
- No arithmetic operations needed on mode codes (diagnostic value only)
- Uppercase hex with leading zeros provides visual consistency

**Breaking change acknowledged:** Existing numeric sensor will become unavailable, requiring users to update dashboards. This is acceptable given low usage (diagnostic sensor) and significant usability improvement.

**Implementation:** Use snprintf with local char buffer of size 5 (to hold "0xXX\0") for safety. Format: snprintf(buffer, sizeof(buffer), "0x%02X", mode_byte).

**3. Remove state_code sensor entirely**

**Decision:** Do not expose status byte as a sensor. Use it internally for composite key matching only.

**Justification:**
- Status byte has no diagnostic value on its own (only meaningful paired with mode byte)
- Reduces entity clutter in Home Assistant (REQ-FN-05)
- Mode/Status/Source text sensors already provide all user-visible state information
- mode_code hex sensor provides diagnostic capability for unknown states
- Simplifies user interface and reduces confusion

**4. Explicit match_any_status flag vs implicit null status**

**Decision:** Use explicit boolean flag `match_any_status` rather than special status value (e.g., 0xFF) to indicate status-agnostic matching.

**Justification:**
- Self-documenting code - intent is clear when reading MODE_MAPPINGS
- Prevents accidental matches if 0xFF becomes a real protocol value
- Aligns with ESPHome coding conventions (explicit over implicit)
- Makes maintenance easier for contributors (REQ-NFN-02)

**5. Member variable for status byte persistence**

**Decision:** Add `uint8_t last_status_byte_` member variable initialized to 0x00.

**Justification:**
- Handles edge case where mode byte arrives before first status byte
- Ensures continuous state reporting without special initialization logic
- Minimal memory overhead (1 byte)
- Follows existing pattern (`last_mode_`, `last_state_`, etc.)

**6. No fallback mapping for mode 0x40 with unknown status**

**Decision:** Mode 0x40 is only mapped for status bytes 0x0F and 0x09. Any other status byte with mode 0x40 will result in "Unknown" state.

**Justification:**
- Mode 0x40 is initialization-specific and only appears during Rain Director boot sequence
- Only status bytes 0x0F (filling from rainwater) and 0x09 (filling from mains) are documented for initialization
- Providing a fallback for 0x40 would be misleading, as it would hide potentially important protocol variations
- Better to show "Unknown" and log the unexpected combination for user reporting than to guess at the meaning
- If additional 0x40 status combinations are discovered, they can be added retroactively

**Implementation:** MODE_MAPPINGS will have no status-agnostic entry for mode 0x40. The composite key lookup will fail to match mode 0x40 with undocumented status bytes, falling through to "Unknown" state handling.

**Alternative considered:** Add fallback entry `{ mode: 0x40, status: 0x00, mode_str: "Init", status_str: "Unknown", source_str: "Unknown", is_refresh: false, match_any_status: true }`. Rejected because it masks unknown protocol behavior that should be investigated.

**7. Status byte display in mode_code - rejected alternative**

**Alternative considered:** Include status byte in mode_code sensor output, e.g., "0x40/0x0F" or "0x40 (status: 0x0F)".

**Decision:** Rejected. mode_code remains mode-byte-only in hex format "0xXX".

**Justification:**
- mode_code is specifically the "mode code" from hex messages, not a composite state indicator
- Mode/Status/Source text sensors already provide human-readable composite state
- Combining mode and status in one diagnostic sensor creates ambiguity about the sensor's purpose
- Users troubleshooting unknown codes benefit from seeing the raw mode byte value
- Keeps sensor naming and purpose clear: mode_code = mode byte, not mode+status combination

### Quality Attributes

**Performance (REQ-NFN-01):**
- Composite key lookup adds worst-case 1 additional comparison per array entry (status byte check)
- With 11 entries: 11 extra comparisons = ~10 clock cycles on ESP32 = <0.01ms overhead
- Well under 10ms threshold specified in REQ-NFN-01
- No impact on UART processing throughput (9600 baud = ~1 byte per 1ms)
- State publication latency unchanged from current implementation

**Maintainability (REQ-NFN-02):**
- MODE_MAPPINGS structure enhanced with inline comments explaining composite key format
- Header comment block provides examples of status-specific vs status-agnostic entries
- Contributing guide updated to explain when to use each pattern
- Structure organization: status-specific entries first, then status-agnostic (clear visual grouping)
- match_any_status flag makes intent explicit
- Complexity increase acknowledged in comments with explanation of benefits

**Backward Compatibility (REQ-NFN-03):**
- Existing mode mappings (0x01, 0x04, 0x08, 0x0C, 0x10) continue to work via match_any_status flag
- No YAML configuration changes required
- Mode/Status/Source text sensors continue to function identically
- mode_code sensor type change is breaking but localized to one diagnostic entity
- Users can update component via package refresh without manual intervention

**Reliability (REQ-NFN-04):**
- Unknown mode/status combinations fall through to "Unknown" state gracefully
- ESP_LOGW warning logs both mode and status in hex format for diagnostics
- Logging distinguishes "status not yet received" from "unknown combination"
- Component continues operating after unknown codes (no crashes)
- mode_code text sensor shows hex value for user debugging
- Graceful degradation maintains core functionality (tank level, consumption tracking)

---

## API Design [Optional if there are public interfaces needed]

Not applicable - this feature modifies internal component behavior only. No public APIs are exposed beyond the existing ESPHome sensor configuration schema, which remains unchanged except for the mode_code sensor type change.

**Sensor Configuration Changes:**

The mode_code sensor configuration changes from:
```python
# OLD (numeric sensor)
cv.Optional(CONF_MODE_CODE): sensor.sensor_schema(
    icon="mdi:cog",
    accuracy_decimals=0,
),
```

To:
```python
# NEW (text sensor)
cv.Optional(CONF_MODE_CODE): text_sensor.text_sensor_schema(
    icon="mdi:cog",
),
```

This is a breaking change for users with existing configurations, but no YAML syntax changes are required - the component handles the migration automatically by changing the sensor type.

---

## Modified Components

### MODE_MAPPINGS Data Structure
**Change Description:**

Currently, MODE_MAPPINGS is a static array of anonymous structures containing `{code, mode, status, source, is_refresh}` where `code` is a single uint8_t mode byte. The lookup algorithm matches only this mode byte.

**Current structure (before):**
```cpp
static const struct {
  uint8_t code;
  const char *mode;
  const char *status;
  const char *source;
  bool is_refresh;
} MODE_MAPPINGS[] = { ... };
```

After modification, MODE_MAPPINGS will contain `{code, status_byte, mode, status, source, is_refresh, match_any_status}`:

**New structure (after):**
```cpp
static const struct {
  uint8_t code;
  uint8_t status_byte;
  const char *mode;
  const char *status;
  const char *source;
  bool is_refresh;
  bool match_any_status;
} MODE_MAPPINGS[] = { ... };
```

Where:
- `code` remains the mode byte (uint8_t)
- `status_byte` is the status byte value to match (uint8_t) - only used if match_any_status is false
- `match_any_status` indicates whether to ignore status_byte during matching (bool)
- Other fields unchanged
- Structure remains anonymous (no typedef needed per ESPHome conventions)

The structure will be reorganized with status-specific entries first (match_any_status = false), followed by status-agnostic entries (match_any_status = true). This ordering ensures priority matching works correctly.

**Dependants:**
- Lookup algorithm in `process_hex_code_()` method - must implement two-tier matching
- Comments and documentation explaining the mapping format

**Kind:** Static const data structure (array of anonymous structs)

**Requirements References:**
- REQ-FN-01: Composite key mode mapping with two-tier priority
- REQ-FN-02: New initialization mode mappings (adds 3 entries)
- REQ-FN-03: New mains backup mode mappings (adds 2 entries)
- REQ-FN-06: Existing mode compatibility with status-agnostic fallback
- REQ-NFN-02: Maintainability through clear structure and comments

**Test Cases:**
- TEST-MODE-MAPPINGS-COMPOSITE: Verify all 5 new composite key entries exist in array with correct values
- TEST-MODE-MAPPINGS-AGNOSTIC: Verify all 6 existing entries have match_any_status=true
- TEST-MODE-MAPPINGS-ORDER: Verify status-specific entries appear before status-agnostic entries in array
- TEST-MODE-MAPPINGS-COMMENTS: Verify header comments explain composite key format with examples

---

### process_hex_code_() Method
**Change Description:**

Currently, this method extracts the mode byte from hex codes, performs a linear search through MODE_MAPPINGS matching only the mode byte, and publishes the mode byte as a numeric value to mode_code_sensor_.

After modification, this method will:
1. Extract mode byte (unchanged)
2. Use `last_status_byte_` member variable for composite key lookup
3. Implement two-tier search: check match_any_status flag, match status_byte only if flag is false
4. Format mode byte as hex string "0x%02X" (uppercase, 2 digits with leading zero) using snprintf with char buffer[5]
5. Publish hex string to mode_code_sensor_ (now a TextSensor)
6. Maintain existing refresh cycle tracking logic (in_refresh_ flag)
7. Log unknown codes with both mode and status bytes in hex format, distinguishing between "status not yet received" and "unknown combination"

**Logging behavior:**
- Maintains ESP_LOGI for normal state changes (Mode/Status/Source transitions)
- Uses ESP_LOGW for unknown mode/status combinations (per REQ-NFN-04)
- Detects whether status byte has been received via `status_byte_received_` flag
- Log format when status not yet received: "Unknown mode 0x%02X (status not yet received)"
- Log format when status received but combination unknown: "Unknown mode/status combination: mode=0x%02X status=0x%02X"

**Dependants:**
- mode_code_sensor_ member variable (changes from Sensor* to TextSensor*)
- MODE_MAPPINGS structure (new fields: status_byte, match_any_status)
- last_status_byte_ member variable (new dependency)
- status_byte_received_ member variable (new dependency for logging edge case)

**Kind:** Protected method in RainDirectorComponent class

**Requirements References:**
- REQ-FN-01: Composite key lookup algorithm with priority matching
- REQ-FN-04: Hexadecimal mode code display format
- REQ-FN-06: Existing mode compatibility
- REQ-NFN-01: Performance (efficient lookup implementation)
- REQ-NFN-04: Unknown state handling with logging

**Test Cases:**
- TEST-PROCESS-HEX-COMPOSITE-MATCH: Process mode 0x00 with last_status_byte_=0x01, verify "Backup/Filling/Mains" published
- TEST-PROCESS-HEX-AGNOSTIC-MATCH: Process mode 0x01 with any status, verify "Normal/Idle/Rainwater" published
- TEST-PROCESS-HEX-PRIORITY: Process mode 0x00 with status 0x00, verify fallback to "Normal/Filling/Rainwater"
- TEST-PROCESS-HEX-FORMAT: Process mode 0xC0, verify mode_code publishes "0xC0" (uppercase)
- TEST-PROCESS-HEX-LEADING-ZERO: Process mode 0x09, verify mode_code publishes "0x09" (not "0x9")
- TEST-PROCESS-HEX-UNKNOWN-STATUS: Process mode 0x40 with status 0x00, verify "Unknown" published (no fallback for 0x40)
- TEST-PROCESS-HEX-UNKNOWN: Process mode 0xFF, verify ESP_LOGW called with both mode and status hex values
- TEST-PROCESS-HEX-REFRESH: Process 0x10 then 0x00, verify in_refresh_ tracking still works
- TEST-PROCESS-HEX-LOGGING: Verify ESP_LOGI for normal state changes, ESP_LOGW for unknown codes

---

### process_json_() Method
**Change Description:**

Currently, this method extracts the status byte from JSON messages and publishes it to state_code_sensor_ (a numeric Sensor).

After modification, this method will:
1. Extract status byte from JSON (unchanged logic)
2. Store value in `last_status_byte_` member variable
3. Set `status_byte_received_` flag to true (enables proper logging in process_hex_code_())
4. Do NOT publish to any sensor (state_code_sensor_ removed)
5. Continue to extract and publish tank level "top" value (unchanged)
6. Remove the ESP_LOGI log for state value (no longer published)

**Dependants:**
- state_code_sensor_ member variable (removed)
- last_status_byte_ member variable (new dependency)
- status_byte_received_ member variable (new dependency)
- set_state_code_sensor() setter method (removed)

**Kind:** Protected method in RainDirectorComponent class

**Requirements References:**
- REQ-FN-01: Status byte used for composite key matching (stored in member variable)
- REQ-FN-05: Remove state code sensor (no publishing)

**Test Cases:**
- TEST-PROCESS-JSON-STORE: Process JSON with state=1, verify last_status_byte_ set to 1
- TEST-PROCESS-JSON-FLAG: Process JSON with state=9, verify status_byte_received_ set to true
- TEST-PROCESS-JSON-NO-PUBLISH: Process JSON with state=9, verify state_code_sensor_ NOT called (removed)
- TEST-PROCESS-JSON-LEVEL: Process JSON with top=50, verify tank_level_sensor_ publishes 50 (unchanged)
- TEST-PROCESS-JSON-FIRST: Process JSON before any hex code, verify no crashes, verify subsequent hex code uses stored status

---

### RainDirectorComponent Class (Header)
**Change Description:**

Currently, the class declares mode_code_sensor_ as `sensor::Sensor*` and state_code_sensor_ as `sensor::Sensor*`. It has setters for both diagnostic sensors.

After modification:
1. Change `mode_code_sensor_` from `sensor::Sensor*` to `text_sensor::TextSensor*`
2. Remove `state_code_sensor_` member variable completely
3. Add `last_status_byte_` member variable (uint8_t, initialized to 0x00)
4. Add `status_byte_received_` member variable (bool, initialized to false) for logging edge case detection
5. Change `set_mode_code_sensor()` parameter from `sensor::Sensor*` to `text_sensor::TextSensor*`
6. Remove `set_state_code_sensor()` setter method
7. Remove state_code_sensor_ from dump_config() logging

**Dependants:**
- Python configuration (__init__.py) - must update mode_code registration
- YAML configuration (rain-director.yaml) - entity type changes in Home Assistant
- dump_config() method - remove state_code logging

**Kind:** Class definition (header file)

**Requirements References:**
- REQ-FN-01: Add last_status_byte_ for composite key storage
- REQ-FN-04: Change mode_code to text sensor for hex format
- REQ-FN-05: Remove state_code sensor

**Test Cases:**
- TEST-HEADER-MODE-CODE-TYPE: Verify mode_code_sensor_ declared as TextSensor*
- TEST-HEADER-STATUS-BYTE: Verify last_status_byte_ declared as uint8_t member
- TEST-HEADER-STATUS-FLAG: Verify status_byte_received_ declared as bool member
- TEST-HEADER-NO-STATE-CODE: Verify state_code_sensor_ and set_state_code_sensor() not present
- TEST-HEADER-SETTER: Verify set_mode_code_sensor() accepts TextSensor* parameter

---

### Python Configuration (__init__.py)
**Change Description:**

Currently, __init__.py registers mode_code as a sensor.Sensor using sensor.sensor_schema() and state_code as a sensor.Sensor. The to_code() function registers both as numeric sensors.

After modification:
1. Change CONF_MODE_CODE to use text_sensor.text_sensor_schema() instead of sensor.sensor_schema()
2. Remove all CONF_STATE_CODE references (schema definition and to_code registration)
3. Update set_mode_code_sensor() call to use text_sensor.new_text_sensor()
4. Remove set_state_code_sensor() call

**Dependants:**
- RainDirectorComponent C++ class (expects TextSensor for mode_code)
- User YAML configurations (mode_code becomes text sensor, state_code removed)

**Kind:** Python module (ESPHome configuration validator)

**Requirements References:**
- REQ-FN-04: mode_code becomes text sensor for hex display
- REQ-FN-05: Remove state_code sensor
- REQ-NFN-03: Backward compatibility (no YAML config changes required)

**Test Cases:**
- TEST-PYTHON-MODE-CODE-SCHEMA: Verify CONF_MODE_CODE uses text_sensor.text_sensor_schema()
- TEST-PYTHON-NO-STATE-CODE: Verify CONF_STATE_CODE constant and schema entry removed
- TEST-PYTHON-REGISTRATION: Verify to_code() calls text_sensor.new_text_sensor() for mode_code
- TEST-PYTHON-NO-STATE-SETTER: Verify to_code() does not call set_state_code_sensor()

---

### dump_config() Method
**Change Description:**

Currently, dump_config() logs mode_code as LOG_SENSOR and state_code as LOG_SENSOR.

After modification:
1. Change mode_code logging from LOG_SENSOR to LOG_TEXT_SENSOR
2. Remove state_code logging line entirely

**Dependants:** None (diagnostic method only)

**Kind:** Public method in RainDirectorComponent class

**Requirements References:**
- REQ-FN-04: mode_code is now text sensor
- REQ-FN-05: Remove state_code sensor

**Test Cases:**
- TEST-DUMP-CONFIG-MODE-CODE: Verify dump_config() uses LOG_TEXT_SENSOR for mode_code
- TEST-DUMP-CONFIG-NO-STATE: Verify dump_config() does not log state_code

---

## Added Components

No new components are added. This feature modifies existing components only.

---

## Documentation Considerations

**README.md Updates:**
1. Update "Known Mode Codes" section to document the 5 new codes with their status byte requirements:
   - 0xC0 + Status 0x0F = Init/Draining/Rainwater
   - 0x40 + Status 0x0F = Init/Filling/Rainwater
   - 0x40 + Status 0x09 = Init/Filling/Mains
   - 0x02 + Status 0x01 = Backup/Idle/Mains
   - 0x00 + Status 0x01 = Backup/Filling/Mains

2. Update "Communication Protocol" section to explain composite key matching:
   - Explain that some modes require specific status bytes for correct interpretation
   - Document the two-tier matching priority system
   - Note that status byte is used internally but not exposed as a sensor

3. Update "Sensors" section:
   - Note that mode_code now displays in hexadecimal format (0xXX)
   - Remove state_code from sensor list
   - Add migration note: "Breaking change in v1.1.0: mode_code changed from numeric to text sensor. Users with dashboards referencing this sensor will need to update their configurations."

4. Add troubleshooting entry:
   - "Mode shows as 'Unknown' during initialization: This is expected during brief transitions. Init modes appear only during Rain Director boot-up."

5. Add state diagram showing refresh cycle sequence:
   - Illustrate the flow: Normal → Refresh (in_refresh_ = true) → Normal (in_refresh_ = false)
   - Show how in_refresh_ flag tracks refresh cycle state
   - Explain that refresh cycle is detected via mode 0x10, then exit is detected when next non-0x10 mode arrives

**Inline Code Comments:**
1. Add comprehensive header comment to MODE_MAPPINGS explaining:
   - Composite key format: {code, status_byte, mode, status, source, is_refresh, match_any_status}
   - Matching priority: status-specific first, then status-agnostic fallback
   - When to use match_any_status=true vs false
   - Examples of each pattern
   - Note that mode 0x40 intentionally has no fallback - other status bytes for 0x40 will map to "Unknown"

2. Update existing "Add new discovered codes here" comment to explain how to add:
   - Status-specific codes (set match_any_status=false, specify exact status_byte)
   - Status-agnostic codes (set match_any_status=true, status_byte ignored)

3. Add comment in process_hex_code_() explaining the two-tier lookup algorithm

**No separate documentation files needed** - this is a single ESPHome component with all documentation in README.md per project guidelines.

---

## Test Strategy

### Test Pyramid

**Unit Tests:**
This project does not use automated unit tests per project guidelines (see `.sdd/project-guidelines.md`). Testing conventions specify manual validation via hardware testing.

**Integration Tests:**
Not applicable - ESPHome custom components are tested through hardware integration only.

**E2E Tests:**
Manual end-to-end validation on actual ESP32 hardware with Rain Director controller.

### Coverage Strategy

**Critical Paths:**
1. **Composite key matching logic:** Verify status-specific matches take priority over status-agnostic fallback
2. **Mode code hex formatting:** Verify uppercase format with leading zeros
3. **Unknown state handling:** Verify graceful degradation and logging
4. **Backward compatibility:** Verify existing modes continue to work
5. **Status byte persistence:** Verify last_status_byte_ maintains value across mode updates
6. **JSON-first scenario:** Verify behavior when JSON arrives before hex codes during ESP32 boot

**Performance Tests:**
- Monitor lookup overhead with ESP_LOGD timestamps before and after MODE_MAPPINGS loop
- Measure time delta between start of lookup and completion (expected <0.01ms)
- Verify state updates occur within 1 second of UART message receipt (REQ-NFN-01 total latency requirement)
- Note: 1 second total latency requirement is distinct from <10ms lookup overhead measurement
- Lookup overhead measurement: Add ESP_LOGD before loop entry and after loop exit, calculate difference
- Total latency measurement: Time from UART byte arrival to text sensor publish_state() completion
- No automated performance benchmarks (hardware-dependent)

**Security Tests:**
Not applicable - no security requirements for this feature.

**Accessibility Tests:**
Not applicable - this is a data integration component, not a user interface.

### Test Data

**Required Test Scenarios:**
1. Rain Director boot sequence (Init modes):
   - Trigger by power cycling Rain Director controller
   - Expected sequence: 0xC0/0x0F → 0x40/0x0F → 0x40/0x09 → Normal mode

2. Rainwater tank empty scenario (Backup modes):
   - Drain rainwater tank to empty
   - Expected behavior: Mode 0x02/0x01 (Backup Idle), then 0x00/0x01 (Backup Filling)

3. Normal operation modes:
   - Mode 0x01 with various status bytes (verify status-agnostic matching)
   - Mode 0x00 with status 0x00 vs 0x01 (verify priority matching)

4. Unknown codes:
   - Inject unknown mode byte (e.g., 0xFF) via UART
   - Verify "Unknown" published and ESP_LOGW logged

5. JSON-first scenario:
   - Restart ESP32 while Rain Director is running (mid-stream boot)
   - First message received is JSON status byte
   - Verify no crash, verify status byte stored
   - Verify subsequent hex code uses stored status correctly

6. Mode 0x40 with unknown status:
   - Inject mode 0x40 with status 0x00 (undocumented combination)
   - Verify "Unknown" published (no fallback for 0x40)
   - Verify ESP_LOGW logs the unexpected combination

### Test Feasibility

**No blockers to testing:**
- All test scenarios can be validated with existing Rain Director hardware
- Init modes tested by power cycling controller (simple)
- Backup modes tested by draining tank (requires manual intervention but feasible)
- Unknown codes tested by modifying component to inject test data (development only)
- JSON-first scenario tested by restarting ESP32 while Rain Director running (simple)

**Manual Test Procedure:**
1. Flash modified component to ESP32
2. Monitor ESPHome logs and Home Assistant sensors simultaneously
3. Trigger each test scenario (power cycle, drain tank, ESP32 restart, etc.)
4. Verify sensor values match expected mode/status/source strings
5. Verify mode_code displays in hex format
6. Verify state_code sensor does not appear in Home Assistant
7. Check ESPHome logs for correct ESP_LOGW on unknown codes
8. Verify logging distinguishes "status not yet received" from "unknown combination"

---

## Risks and Dependencies

**Technical Risks:**

1. **Risk:** Mode and status bytes arrive at different times, causing temporary incorrect states
   - **Likelihood:** Low - both messages arrive on same UART within milliseconds
   - **Impact:** Low - state would self-correct on next message
   - **Mitigation:** Use last_status_byte_ persistence to minimize window of incorrect state
   - **Contingency:** If timing issues arise, consider buffering mode bytes until status byte updates

2. **Risk:** Unknown edge case mode/status combinations not in specification
   - **Likelihood:** Medium - protocol is reverse-engineered, may have undiscovered states
   - **Impact:** Low - graceful degradation to "Unknown" prevents crashes
   - **Mitigation:** Comprehensive logging of unknown codes for user reporting
   - **Contingency:** Add mappings retroactively via package updates when discovered

3. **Risk:** Breaking change to mode_code sensor disrupts existing user dashboards
   - **Likelihood:** High - all users with mode_code in dashboards affected
   - **Impact:** Medium - requires manual dashboard reconfiguration
   - **Mitigation:** Document breaking change clearly in README and release notes
   - **Contingency:** Provide migration guide showing how to update dashboard YAML

4. **Risk:** Composite key lookup adds latency exceeding REQ-NFN-01 threshold
   - **Likelihood:** Very Low - <10ms easily achievable with 11 entries
   - **Impact:** Low - slight delay in state updates
   - **Mitigation:** Linear search is optimal for small array sizes on ESP32
   - **Contingency:** Profile with ESP_LOGD timestamps if issues reported

**External Dependencies:**

1. **ESPHome framework:** Requires text_sensor component
   - Status: Already included in project (text_sensor used for Mode/Status/Source)
   - Risk: None - no version changes needed

2. **Rain Director protocol stability:** Assumes mode/status byte semantics remain consistent
   - Status: Protocol appears stable based on user reports
   - Risk: Low - manufacturer unlikely to change protocol in existing units
   - Contingency: Make mappings configurable in future version if protocol changes

**Assumptions:**

1. Status byte arrives before or shortly after first mode byte during initialization
   - Basis: Both transmitted on same UART stream during boot
   - Validation: Test by monitoring UART during Rain Director power-on

2. The 5 new mode/status combinations are accurate and complete
   - Basis: Provided by stakeholder who reverse-engineered protocol
   - Validation: Manual testing during Rain Director boot and tank empty scenarios

3. Users can tolerate breaking change to mode_code sensor type
   - Basis: mode_code is diagnostic sensor, not critical for core functionality
   - Validation: Documented clearly with migration guidance

**Constraints:**

1. Must maintain ESPHome coding conventions (ESP_LOG macros, snake_case, etc.)
2. Must work within ESP32 memory constraints (32-bit microcontroller)
3. Cannot introduce external library dependencies (ESPHome custom component limitation)
4. Must remain compatible with ESPHome package import system (GitHub-based updates)

---

## Feasability Review

**Prerequisites - None Required:**

This feature can be implemented immediately without separate prerequisite work. All required infrastructure exists:
- UART communication already receives both mode and status bytes
- Text sensor support already exists (used for Mode/Status/Source)
- MODE_MAPPINGS structure already exists and just needs extension
- No new external components or frameworks required

**Feasibility Assessment:**

All requirements are technically feasible with the existing architecture:
- REQ-FN-01: Composite key matching is straightforward array iteration with two comparisons
- REQ-FN-02 & REQ-FN-03: Adding 5 new mappings is trivial data structure update
- REQ-FN-04: Hex formatting available via standard snprintf function
- REQ-FN-05: Removing sensor requires deletion only, no new code
- REQ-FN-06: Existing modes work via match_any_status flag

No blockers identified. Implementation can proceed immediately.

---


## Task Breakdown

> **CRITICAL: Tests are written WITH implementation, not after.**
> Each task that adds or modifies functionality MUST include writing tests as part of that task.
> Do NOT create separate "Add tests" tasks or defer testing to later phases.
> TDD approach: Write failing test → Implement → Verify test passes → Refactor.

### Phase 1: Data Structure and Infrastructure (Foundation)

**Goal:** Extend MODE_MAPPINGS structure and add status byte persistence without changing behavior. This creates the foundation for composite key matching.

- Task 1.1: Add last_status_byte_ and status_byte_received_ member variables to RainDirectorComponent class
  - Status: Complete
  - Add uint8_t last_status_byte_ private member in rain_director.h
  - Add bool status_byte_received_ private member in rain_director.h
  - Initialize last_status_byte_ to 0x00 in constructor or member initializer list
  - Initialize status_byte_received_ to false in constructor or member initializer list
  - Tests: Build verification (compiles without errors)

- Task 1.2: Update MODE_MAPPINGS structure to include status_byte and match_any_status fields
  - Status: Complete
  - Add status_byte (uint8_t) and match_any_status (bool) fields to anonymous struct
  - Set match_any_status=true for all existing 6 entries
  - Set status_byte=0x00 for existing entries (value ignored when match_any_status=true)
  - Add comprehensive header comment explaining composite key format with examples
  - Add note in comments that mode 0x40 intentionally has no fallback mapping
  - Organize entries: status-specific first (none yet), then status-agnostic (all 6 existing)
  - Tests: TEST-MODE-MAPPINGS-AGNOSTIC, TEST-MODE-MAPPINGS-ORDER, TEST-MODE-MAPPINGS-COMMENTS

- Task 1.3: Modify process_json_() to store status byte in last_status_byte_ and set flag
  - Status: Complete
  - Update process_json_() to set this->last_status_byte_ when status value extracted
  - Set this->status_byte_received_ = true when status byte stored
  - Keep existing state_code_sensor_ publishing (removed in later phase)
  - Keep existing ESP_LOGI logging
  - Tests: TEST-PROCESS-JSON-STORE, TEST-PROCESS-JSON-FLAG, TEST-PROCESS-JSON-FIRST

### Phase 2: Composite Key Lookup (Core Logic)

**Goal:** Implement two-tier composite key matching algorithm while maintaining backward compatibility.

- Task 2.1: Implement composite key lookup algorithm in process_hex_code_()
  - Status: Complete
  - Modify lookup loop to check match_any_status flag
  - If match_any_status=false, compare both mode byte AND last_status_byte_
  - If match_any_status=true, compare mode byte only (existing behavior)
  - First match wins (implements priority: status-specific before status-agnostic)
  - Keep all existing mode_byte publishing and refresh tracking logic
  - Tests: TEST-PROCESS-HEX-COMPOSITE-MATCH, TEST-PROCESS-HEX-AGNOSTIC-MATCH, TEST-PROCESS-HEX-PRIORITY, TEST-PROCESS-HEX-REFRESH

- Task 2.2: Add 5 new mode/status mappings to MODE_MAPPINGS
  - Status: Complete
  - Add entries for 0xC0/0x0F, 0x40/0x0F, 0x40/0x09, 0x02/0x01, 0x00/0x01 with match_any_status=false
  - Place new entries at beginning of array (status-specific matches checked first)
  - Add inline comments explaining each new Init and Backup mode
  - Verify in comments that 0x40 has no status-agnostic fallback (intentional design decision)
  - Tests: TEST-MODE-MAPPINGS-COMPOSITE, TEST-MODE-MAPPINGS-ORDER

- Task 2.3: Update unknown code logging to include status byte and handle edge cases
  - Status: Complete
  - Modify ESP_LOGW in process_hex_code_() to check status_byte_received_ flag
  - If status_byte_received_ is false, log: "Unknown mode 0x%02X (status not yet received)"
  - If status_byte_received_ is true, log: "Unknown mode/status combination: mode=0x%02X status=0x%02X"
  - Ensures logging distinguishes initialization edge case from actual unknown combinations
  - Tests: TEST-PROCESS-HEX-UNKNOWN, TEST-PROCESS-HEX-LOGGING, TEST-PROCESS-HEX-UNKNOWN-STATUS

### Phase 3: mode_code Sensor Type Change (Breaking Change)

**Goal:** Change mode_code from numeric sensor to text sensor displaying hex format.

- Task 3.1: Change mode_code_sensor_ to TextSensor in C++ class
  - Status: Complete
  - In rain_director.h: Change mode_code_sensor_ from sensor::Sensor* to text_sensor::TextSensor*
  - Change set_mode_code_sensor() parameter type to text_sensor::TextSensor*
  - Update dump_config() to use LOG_TEXT_SENSOR instead of LOG_SENSOR
  - Tests: TEST-HEADER-MODE-CODE-TYPE, TEST-HEADER-SETTER, TEST-DUMP-CONFIG-MODE-CODE

- Task 3.2: Update process_hex_code_() to publish hex string format with safe buffer
  - Status: Complete
  - Declare local char buffer[5] for hex string storage (holds "0xXX\0")
  - Use snprintf(buffer, sizeof(buffer), "0x%02X", mode_byte) for safe formatting
  - Call mode_code_sensor_->publish_state(buffer)
  - Tests: TEST-PROCESS-HEX-FORMAT, TEST-PROCESS-HEX-LEADING-ZERO

- Task 3.3: Update Python configuration to register mode_code as text sensor
  - Status: Complete
  - In __init__.py: Change CONF_MODE_CODE from sensor.sensor_schema() to text_sensor.text_sensor_schema()
  - Remove unit_of_measurement and accuracy_decimals parameters (not applicable to text sensor)
  - In to_code(): Change sensor.new_sensor() to text_sensor.new_text_sensor()
  - Tests: TEST-PYTHON-MODE-CODE-SCHEMA, TEST-PYTHON-REGISTRATION

### Phase 4: Remove state_code Sensor (Cleanup)

**Goal:** Remove state_code sensor entirely as it's no longer needed.

- Task 4.1: Remove state_code_sensor_ from C++ class
  - Status: Backlog
  - In rain_director.h: Remove state_code_sensor_ member variable
  - Remove set_state_code_sensor() setter method
  - Remove state_code_sensor_ from dump_config() LOG_SENSOR call
  - Tests: TEST-HEADER-NO-STATE-CODE, TEST-DUMP-CONFIG-NO-STATE

- Task 4.2: Remove state_code publishing from process_json_()
  - Status: Backlog
  - Remove state_code_sensor_->publish_state() call
  - Remove ESP_LOGI log for state value
  - Keep last_top_ tank level processing (unchanged)
  - Keep last_status_byte_ storage (added in Phase 1)
  - Keep status_byte_received_ flag setting (added in Phase 1)
  - Tests: TEST-PROCESS-JSON-NO-PUBLISH, TEST-PROCESS-JSON-LEVEL

- Task 4.3: Remove state_code from Python configuration
  - Status: Backlog
  - In __init__.py: Remove CONF_STATE_CODE constant definition
  - Remove CONF_STATE_CODE from CONFIG_SCHEMA
  - Remove state_code registration from to_code() function
  - Tests: TEST-PYTHON-NO-STATE-CODE, TEST-PYTHON-NO-STATE-SETTER

### Phase 5: Documentation and Release (Finalization)

**Goal:** Update documentation to reflect composite key system and breaking changes.

- Task 5.1: Update README.md with new mode codes, composite key explanation, and state diagram
  - Status: Backlog
  - Add 5 new mode codes to "Known Mode Codes" section with status byte requirements
  - Update "Communication Protocol" section to explain composite key matching priority
  - Document that status byte is used internally but not exposed as sensor
  - Add migration note about mode_code breaking change (numeric to text)
  - Add troubleshooting entry for "Unknown" during Init modes
  - Add state diagram showing refresh cycle sequence and in_refresh_ flag behavior
  - Tests: Manual review of documentation clarity and accuracy

- Task 5.2: Update MODE_MAPPINGS header comments with examples
  - Status: Backlog
  - Expand existing header comment to explain composite key format
  - Add examples showing when to use match_any_status=true vs false
  - Document matching priority (status-specific first, then status-agnostic)
  - Add contributing guidance for adding new discovered codes
  - Clarify that mode 0x40 intentionally has no fallback (design decision documented)
  - Tests: Manual review by stakeholder for clarity

- Task 5.3: End-to-end validation on hardware
  - Status: Backlog
  - Flash final build to ESP32 hardware
  - Power cycle Rain Director to verify Init sequence appears correctly
  - Drain tank to verify Backup modes appear correctly
  - Verify existing modes (Normal, Holiday, Refresh) still work
  - Verify mode_code displays in hex format
  - Verify state_code sensor is gone
  - Check Home Assistant dashboard shows all sensors correctly
  - Monitor logs for any unexpected warnings or errors
  - Tests: TEST-PROCESS-HEX-COMPOSITE-MATCH, TEST-PROCESS-HEX-AGNOSTIC-MATCH, TEST-PROCESS-HEX-PRIORITY, TEST-PROCESS-HEX-FORMAT, TEST-PROCESS-HEX-LEADING-ZERO, TEST-PROCESS-HEX-UNKNOWN-STATUS, TEST-PROCESS-HEX-UNKNOWN, TEST-PROCESS-HEX-REFRESH, TEST-PROCESS-HEX-LOGGING, TEST-PROCESS-JSON-STORE, TEST-PROCESS-JSON-FLAG, TEST-PROCESS-JSON-NO-PUBLISH, TEST-PROCESS-JSON-LEVEL, TEST-PROCESS-JSON-FIRST, TEST-MODE-MAPPINGS-COMPOSITE, TEST-MODE-MAPPINGS-AGNOSTIC, TEST-MODE-MAPPINGS-ORDER, TEST-MODE-MAPPINGS-COMMENTS

---

## Intermediate Dead Code Tracking

> Code introduced in earlier phases that will be used in later phases must be tracked here.
> All entries must be resolved (code used or removed) by the final phase.

| Phase Introduced | Description | Used In Phase | Status |
|------------------|-------------|---------------|--------|
| Phase 1 | match_any_status field in MODE_MAPPINGS structure | Phase 2 | Resolved in Phase 2 Task 2.1 (used by composite key lookup) |
| Phase 1 | status_byte field in MODE_MAPPINGS structure | Phase 2 | Resolved in Phase 2 Task 2.1 (used by composite key lookup) |
| Phase 1 | last_status_byte_ member variable | Phase 2 | Resolved in Phase 2 Task 2.1 (used by composite key lookup) |
| Phase 1 | status_byte_received_ member variable | Phase 2 | Resolved in Phase 2 Task 2.3 (used by logging logic) |

**Note:** All intermediate code from Phase 1 is immediately used in Phase 2, so there is minimal dead code window. No code remains unused by final phase.

---

## Test Stub Tracking

> **CRITICAL: Test stubs are NOT acceptable without explicit tracking.**
> All tests MUST be fully implemented as part of the task that introduces the code they test.
> If a test stub is absolutely necessary (e.g., external dependency not yet available), it MUST be tracked here.
> All entries must be resolved (stub implemented or removed) by the final phase.
> A "stub" includes: `skip`, `todo`, `pass`, `pytest.mark.skip`, `@unittest.skip`, `it.skip`, `xit`, `pending`, empty test bodies, or `assert True` placeholders.

| Phase Introduced | Test Name | Reason for Stub | Implemented In Phase | Status |
|------------------|-----------|-----------------|----------------------|--------|
| N/A | N/A | N/A | N/A | N/A |

**Note:** This project uses manual hardware testing per project guidelines. No automated test stubs are used. All test cases documented in component sections are validated manually via hardware integration during the task that implements the functionality.

---

## Requirements Validation

Ensure all requirements have matching tasks

- REQ-FN-01: Composite Key Mode Mapping
  - Phase 1 Task 1.1: Add last_status_byte_ and status_byte_received_ member variables
  - Phase 1 Task 1.2: Update MODE_MAPPINGS structure
  - Phase 1 Task 1.3: Store status byte from JSON
  - Phase 2 Task 2.1: Implement composite key lookup algorithm

- REQ-FN-02: New Initialization Mode Mappings
  - Phase 2 Task 2.2: Add 5 new mode/status mappings (includes 3 Init modes)

- REQ-FN-03: New Mains Backup Mode Mappings
  - Phase 2 Task 2.2: Add 5 new mode/status mappings (includes 2 Backup modes)

- REQ-FN-04: Hexadecimal Mode Code Display
  - Phase 3 Task 3.1: Change mode_code_sensor_ to TextSensor
  - Phase 3 Task 3.2: Publish hex string format
  - Phase 3 Task 3.3: Update Python configuration for text sensor

- REQ-FN-05: Remove State Code Sensor
  - Phase 4 Task 4.1: Remove state_code_sensor_ from C++ class
  - Phase 4 Task 4.2: Remove state_code publishing from process_json_()
  - Phase 4 Task 4.3: Remove state_code from Python configuration

- REQ-FN-06: Existing Mode Compatibility
  - Phase 1 Task 1.2: Set match_any_status=true for existing entries
  - Phase 2 Task 2.1: Implement fallback matching for status-agnostic modes
  - Phase 5 Task 5.3: E2E validation of existing modes

- REQ-NFN-01: State Update Latency
  - Phase 2 Task 2.1: Efficient composite key lookup algorithm
  - Phase 5 Task 5.3: E2E validation of update latency

- REQ-NFN-02: Data Structure Maintainability
  - Phase 1 Task 1.2: Add comprehensive header comments
  - Phase 5 Task 5.2: Update MODE_MAPPINGS comments with examples

- REQ-NFN-03: Backward Compatibility
  - Phase 1 Task 1.2: Preserve existing entries with match_any_status flag
  - Phase 2 Task 2.1: Implement fallback matching
  - Phase 5 Task 5.3: E2E validation of upgrade scenario

- REQ-NFN-04: Unknown State Handling
  - Phase 2 Task 2.3: Update unknown code logging to include status byte
  - Phase 5 Task 5.3: E2E validation of unknown code handling

---

## Appendix

### Glossary

**Composite Key:** A lookup key consisting of both mode byte (from hex codes) and status byte (from JSON messages) used together to uniquely identify a Rain Director operational state.

**Status-Specific Mapping:** A MODE_MAPPINGS entry that requires both a specific mode byte AND a specific status byte to match. Implemented by setting match_any_status=false and specifying the exact status_byte value.

**Status-Agnostic Mapping:** A MODE_MAPPINGS entry that matches a mode byte regardless of status byte value. Used for modes where the status byte doesn't affect interpretation. Implemented by setting match_any_status=true.

**Fallback Mapping:** A status-agnostic mapping used when no status-specific mapping matches for a given mode byte. Provides default interpretation when status byte is unknown or irrelevant.

**Two-Tier Matching:** The lookup algorithm's priority system where status-specific mappings are checked first (exact mode + status match), and status-agnostic mappings are used as fallback (mode match only).

**Init Mode:** Initialization mode that occurs during Rain Director boot-up sequence, involving draining and refilling the header tank to establish baseline operation. Identified by mode bytes 0xC0 and 0x40 with specific status bytes.

**Backup Mode:** Operational mode where the Rain Director operates exclusively on mains water because the rainwater tank is empty. Identified by mode bytes 0x02 and 0x00 with status byte 0x01.

**MODE_MAPPINGS:** C++ static const array structure that maps mode/status byte combinations to human-readable strings (Mode, Status, Source) and operational flags (is_refresh, match_any_status).

**Mode Byte:** The single-byte hex code sent by the Rain Director display panel (device 10) in the format <1053[MODE_BYTE]...>. Represents the primary operational state.

**Status Byte:** The single-byte decimal value from JSON messages in the format {"tanklevels":{"state":"[STATUS_BYTE]",...}}. Provides additional state context required to disambiguate certain mode bytes.

**Breaking Change:** A modification that causes existing functionality to behave differently or become unavailable, requiring user intervention. The mode_code sensor changing from numeric to text type is a breaking change.

**match_any_status Flag:** Boolean field in MODE_MAPPINGS structure indicating whether the status_byte field should be checked during lookup. When true, only mode byte is matched (status-agnostic). When false, both mode and status must match (status-specific).

**last_status_byte_:** Member variable that persists the most recently received status byte value, ensuring continuous state reporting when mode bytes arrive without synchronized status bytes.

**status_byte_received_:** Boolean flag indicating whether at least one JSON message with status byte has been processed. Used to distinguish "status not yet received" from "unknown mode/status combination" in logging.

### References

- Specification Document: `/home/pete/dev/esphome-rain-director/.sdd/init-backup-modes/specification.md`
- Project Guidelines: `/home/pete/dev/esphome-rain-director/.sdd/project-guidelines.md`
- Current Implementation: `/home/pete/dev/esphome-rain-director/components/rain_director/rain_director.cpp`
- ESPHome Text Sensor Documentation: https://esphome.io/components/text_sensor/
- ESPHome Sensor Documentation: https://esphome.io/components/sensor/
- ESPHome Custom Component Guide: https://esphome.io/custom/custom_component.html
- Project Repository: https://github.com/pturner1989/esphome-rain-director

### Change History
| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-07 | Claude (Technical Architect) | Initial design based on specification v1.1 and codebase exploration |
| 1.1 | 2026-01-07 | Claude (Technical Architect) | Applied design review fixes: Added test case IDs to tasks, added JSON-first edge case handling, clarified mode 0x40 fallback priority, added logging validation, defined C++ struct explicitly, specified hex buffer safety, clarified performance measurement, enhanced unknown code logging, documented status byte display alternative, added refresh cycle state diagram |

---
