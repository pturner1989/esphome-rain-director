# Research: Protocol Discovery

**Feature:** protocol-discovery
**Date:** 2026-09-14
**Status:** Draft
**Requirement ID prefix:** `PROTOCOL-DISCOVERY-REQ-FN-NN` / `PROTOCOL-DISCOVERY-REQ-NFN-NN`

---

## 1. Problem Context

### What the user wants

Pete wants to read all the serial data coming from the Rain Director so he can log it and discover
commands and other items. He wants to learn how to set the command modes: change source, refresh,
and holiday mode.

The method he has in mind is direct. Press a button on the Rain Director control panel, capture
the bytes that appear on the RS-485 bus at that moment, then send those same bytes back to the bus
from the ESP32 and see whether the controller obeys.

### Background: why he wants it

This section is context only. It is not part of this feature.

During long dry spells the underground rainwater tank runs low. The pump has a dry-run protection
backoff mechanism, and that mechanism cannot be controlled from the Rain Director. To avoid
tripping it, Pete switches the system to mains water early, before the rainwater tank runs dry, so
the pump holds backpressure and stays pressurised instead of running dry. Today he has to walk to
the cupboard to do this. He wants to do it remotely.

Pete also stated that the Rain Director detects the rainwater tank is empty by opening the
rainwater valve and observing that the tank level does not increase.

### Scope, stated explicitly

Pete narrowed the work himself:

> "You're digging too much into it. I just want you to make it so i can findout if i can read the
> commands when I press buttons on the control panel and then replicate those. The rest I will
> handle."

So this feature covers two things and nothing more:

1. Capture raw bus traffic, so every byte on the wire is visible in the log.
2. Replay captured frames, so arbitrary bytes can be sent back onto the bus on demand.

The automation and control logic is Pete's own follow-on work and is out of scope.

### Why it matters now

Nobody has ever seen the full stream. The component keeps four values and silently discards
everything else. Without a capture there is no data to design against, so no command feature can
be specified. This work removes that blockage, and it is mostly configuration.

---

## 2. Current System Behaviour

The component is receive-only and deliberately lossy. It is also silent about what it discards.

All line references are to `components/rain_director/rain_director.cpp` unless stated otherwise.

### The read loop

`RainDirectorComponent::loop()` (line 112) reads every available byte one at a time with
`this->read()`. It splits the stream on `\r`, `\n`, or `<`. If the buffer grows past 500
characters it is cleared and the content is lost (line 144). Complete chunks go to
`process_buffer_()` (line 151), which routes to `process_hex_code_()` (line 170) or
`process_json_()` (line 311).

### What reaches a sensor

Only four things:

| Input | Result |
|---|---|
| `<2053[LEVEL]80[CHECKSUM]` | Tank level percent |
| `<1053[MODE][SUB][CHECKSUM]` | Mode byte, looked up in `MODE_MAPPINGS` |
| JSON `{"tanklevels":...}` key `"top"` | Tank level percent |
| JSON key `"state"` | Status byte, stored in `last_status_byte_`, used internally for the composite key lookup, not exposed as a sensor |

### What is discarded, and silently

- **Explicit early returns with no logging** (lines 193-201) for `20123`, `2071`, `2010`, `30123`,
  `40123`, `4071`, `4010` and `4033`. These are heartbeat and version frames from devices 20, 30
  and 40.
- **No final else branch in `process_hex_code_()`.** Any frame that is not `2053` or `1053` falls
  off the end of the function and vanishes with no warning at all. This includes any `<10xx` frame
  that is not `1053`. If the display panel emits a distinct frame when a button is pressed, it is
  being dropped right now and the user would never know.
- **Checksums.** Never computed, never validated, never recorded. The parser reads
  `hex.substr(4, 2)` and ignores the trailing bytes.
- **JSON fields.** `extract_json_int_()` (line 329) pulls exactly two keys out of the object and
  discards everything else.
- **The `SUB` byte.** For `1053` frames only the mode byte is read. The `SUB` byte is never
  examined.
- **Plain text.** Boot banners, malformed frames and any non-frame text are lost.

### No captured log exists

There is no raw capture anywhere in the repository. The only protocol samples in `.sdd/`, `docs/`
and `README.md` are two synthetic examples, `<1053000080` and `<20535080`. Every protocol
statement made in the project so far rests on the slice of the stream the parser happens to keep.

### A consequence worth recording

In `rain-director.yaml` (lines 99-106) the consumption tracking lambda branches on
`id(water_source).state` being exactly `"Rainwater"` or `"Mains"`. When the source is `"Unknown"`,
neither branch runs. Water is consumed but counted against nothing. Every unknown state is a
silent hole in the rainwater and mains litre totals, not just a wrong dashboard label.

---

## 3. Domain Constraints and Terminology

### Bus topology

Pete raised a concern that there is no spare RJ45 socket, because the second one is used by the
level sensor. This does not matter.

The component already receives frames from four distinct device IDs through a single socket:
`10` (display panel), `20` (level sensor), `30`, and `40`. Both RJ45 sockets are wired in parallel
onto the same RS-485 differential pair, which is why the README calls them interchangeable. RS-485
is multi-drop: every device sees every frame. Every frame the display panel puts on the bus
already arrives at GPIO16 and is then discarded.

The button-press experiment needs no rewiring.

### The hardware can already transmit

GPIO17 is wired to the MAX485 DI input. The module is auto-direction, so no DE/RE control line is
needed. There is no electrical blocker to writing to the bus. The component simply never calls
`write_array()`.

### No documentation and no prior art

A web search found no existing project covering this controller. This repository is the only
implementation that exists.

The manufacturer's Rain Hub technical portal
(https://www.rainwaterharvesting.co.uk/rain-hub-technical-portal/) has no manuals, no datasheets,
and no mention of RS-485, a serial protocol, or BMS integration.

The product page (https://www.rainwaterharvesting.co.uk/product/rain-director/) describes the
modes only in user terms:

- Holiday Mode "recycl[es] the water in the loft space every 3 days to prevent stagnation".
- Mains Water Mode "divert[s] the home onto mains water, so rainwater can be conserved for the
  garden if hosepipe bans are being introduced".
- Automatic mains backup when the underground tank runs low.

Everything must be learned from the bus.

### Mode definitions, as Pete described them

Pete corrected the vendor's framing with the actual observed behaviour:

- **Holiday mode:** empties the loft tank and refills it from mains water.
- **Refresh mode:** empties the loft tank and refills it from rainwater, or from mains if the
  rainwater tank is empty.

### An unverified hypothesis

The JSON message is named `tanklevels` (plural), and the one level key the parser reads is
`"top"`. The prior SDD documents consistently write the payload with a trailing ellipsis,
`{"tanklevels":{"state":"...",...}}`, which means other fields were observed but never identified.

It is possible the underground rainwater tank level is already present in that JSON and is being
discarded. This is not part of the scope of this feature, but the capture will reveal it at no
extra cost, and it is directly relevant to Pete's follow-on work.

### A hint for the follow-on work

Mode `0x04` is already mapped in `MODE_MAPPINGS` (line 92) as normal mode, idle on mains selected.
The accompanying documentation describes it as "manually selected". That phrasing suggests `0x04`
is the state the panel enters when a person picks mains at the unit, which makes it the likely
target state for a source-switch command.

### Glossary

| Term | Meaning |
|---|---|
| RS-485 | A differential serial signalling standard used for wired device buses. Two wires carry one signal as a voltage difference, which resists electrical noise over long cable runs. |
| Multi-drop | Many devices share one pair of wires. Every device sees every frame that any device sends. |
| Half-duplex | Only one device can transmit at a time. There is no separate return path, so two devices transmitting at once corrupt each other. |
| UART | The serial hardware block in the ESP32 that turns bytes into a stream of bits and back. Runs at 9600 baud here. |
| MAX485 auto-direction | The transceiver chip that converts the ESP32's UART signals to and from RS-485. "Auto-direction" means it switches between transmit and receive on its own, with no control pin from the ESP32. |
| Frame | One complete message on the bus. Rain Director frames start with `<` and end with a line terminator. |
| Checksum | Trailing bytes in a frame, computed from the frame content, used by the receiver to detect corruption. The algorithm here is unknown. |
| Device ID | The first two characters of a frame, identifying the sender. Known: `10` display panel, `20` level sensor, `30` and `40` unidentified. |
| Mode byte | The byte in a `<1053...` frame that indicates the operating mode. |
| Status byte | The `"state"` value in the JSON message. Combined with the mode byte to form the composite lookup key. |
| Header tank / loft tank | The small tank in the loft that feeds the house. Its level is the `"top"` value. |
| Underground tank | The buried rainwater storage tank. Its level may or may not be on the bus. |
| Dry-run protection | The pump's own safety backoff, which trips when it runs with no water. It cannot be controlled from the Rain Director. |
| Replay | Sending back the exact bytes that were captured, unchanged. |
| Synthesis | Constructing a new frame with different values, which requires computing a valid checksum. |

---

## 4. Approaches Considered

### Capture

#### Option A1: ESPHome's built-in UART debugger (chosen)

Add `debug:` under the existing `uart:` block. YAML only, no C++ changes.

This was verified to work in this exact setup:

- `IDFUARTComponent::read_array()` fires
  `this->debug_callback_.call(UART_DIRECTION_RX, data[i])`
  (`venv/lib/python3.13/site-packages/esphome/components/uart/uart_component_esp_idf.cpp`, around
  line 335).
- `UARTDevice::read()` routes to `read_byte()`, which is `read_array(data, 1)`
  (`uart_component.h`, line 57).

So the debugger sees every byte the component consumes, including all the frames the parser throws
away. The ESPHome header `uart_debugger.h` describes the class as most useful "when the debugger is
used to reverse engineer a serial protocol".

`direction: BOTH` also logs transmitted bytes, which lets Pete confirm that a replayed frame
actually went out on the wire.

Configuration options available: `direction` (default `BOTH`), `after:` with `bytes` (default 150),
`timeout` (default 100ms) and `delimiter`, a custom `sequence:`, and `dummy_receiver` (not needed
here, because the component already reads the bytes). The default output action is
`UARTDebug::log_hex(direction, bytes, ':')`.

**Cost.** The built-in log helpers `log_hex`, `log_string`, `log_int` and `log_binary` all log at
`ESP_LOGD` (`uart_debugger.cpp`, lines 113, 157, 175 and 195). The logger level must therefore be
DEBUG for them to appear. The project currently sets `logger: level: INFO`
(`rain-director.yaml`, lines 19-20). ESPHome's own comment also notes that the log buffer limits a
single hex line to about 159 bytes.

#### Option A2: add raw logging inside the custom component (rejected)

Log every byte or every buffer from within `loop()`.

This requires C++ changes and is strictly worse for discovery. The component's `loop()` splits the
stream on `<` and discards buffers over 500 characters, so it can only ever show a processed view.
The built-in debugger shows the true wire bytes.

A2 does have one advantage A1 lacks: it could explicitly warn on unrecognised frames. That is not
needed for button-press discovery, where the goal is to see everything rather than to classify it.

### Replay

#### Option B1: a Home Assistant action calling `uart.write` (chosen)

Expose a user-defined action under `api:` → `actions:` that calls `uart.write` with a templated
payload. YAML only, no C++.

Verified:

- `uart.write` accepts `cv.templatable(validate_raw_data)`
  (`venv/lib/python3.13/site-packages/esphome/components/uart/__init__.py`, around line 501).
- `UARTWriteAction::set_data_template` takes a function returning `std::vector<uint8_t>`
  (`uart/automation.h`, around line 11).
- User-defined actions are configured under `api:` → `actions:`. The older `services:` key was
  renamed to `actions:` in this version
  (`venv/lib/python3.13/site-packages/esphome/components/api/__init__.py`, around line 332).

This gives Pete a Home Assistant service that takes a hex string and sends arbitrary bytes, called
from Developer Tools.

**Why this matters.** Reverse engineering is iterative. If every attempt needs a recompile and a
flash, Pete gets very few experiments per session. A service gives him as many attempts as he can
type, with no reflash.

#### Option B2: the `uart.button` / `uart.switch` platforms (rejected)

These send fixed, predefined byte sequences. Useful once the frames are known. Useless for
discovery, where the bytes are not known yet.

#### Option B3: a `send_command` method in the custom component (rejected)

Add a C++ method that builds a frame and computes its checksum.

Premature. The checksum algorithm is unknown, and as the next section explains, it does not need to
be known.

### Gating the capture

Pete asked for the capture to be gated behind a substitution, so that normal users of the published
repository do not get DEBUG-level log spam.

**Constraint discovered during research:** ESPHome has no config-level conditionals. Substitutions
are plain string replacement (`do_substitution_pass`, `esphome/yaml_util.py`, around line 478) and
there is no `if` in the config schema. A substitution therefore cannot omit the `debug:` block
outright.

Three approaches were identified:

- **A.** Keep `debug:` always present in the config, and set `logger: level: ${log_level}` with a
  default of INFO. At INFO the `ESP_LOGD` calls compile out, so it stays silent. One line to flip,
  but it needs a reflash to change.
- **B (recommended).** Use `debug:` with a custom `sequence:` that logs at INFO behind a runtime
  boolean, exposed as a Home Assistant switch. Capture can be toggled on and off from the dashboard
  with no reflash, and there is no global DEBUG spam from other components.
- **C.** Put the debug configuration in a separate `rain-director-debug.yaml` file, pulled in
  through `packages:` only when wanted.

This was deliberately not settled during research. See section 8.

---

## 5. Recommended Direction

Adopt capture option A1 and replay option B1 together.

This needs no C++ changes and no changes to the custom component. It is a YAML change to
`rain-director.yaml` plus logger configuration. Gate the capture so ordinary users are unaffected,
using approach B unless the design phase decides otherwise.

### The key finding that makes this cheap

Replaying a captured frame does not require cracking the checksum algorithm.

Pete captures the exact bytes the panel sent and sends those same bytes back. The checksum travels
with them. Cracking the checksum is only needed to *synthesise* new frames, that is, to vary a
parameter and compute a fresh checksum for it.

For "press Mains at the panel, capture the frame, replay the frame", byte-for-byte replay avoids
the problem completely. The hardest-looking part of this work is not on the path to the goal.

### Risks and unknowns

These are answerable by experiment, not by analysis. That is the point of the feature.

- It is unknown whether the controller accepts a frame claiming to be device `10` while the real
  display panel is also on the bus, or whether the two conflict.
- Half-duplex collision: the ESP32 can transmit while another device is mid-frame, corrupting it.
  With an auto-direction MAX485 there is no way to sense the bus before transmitting. The worst
  case is a garbled read and a retry. Pete was asked about this risk and said there is no risk to
  be concerned about.
- It is unknown whether the panel emits a distinct command frame on a button press at all, or
  whether it only reports its own resulting state. The capture answers this directly, and it is the
  single most important question the feature exists to settle.

---

## 6. What Was Ruled Out and Why

| Ruled out | Reason |
|---|---|
| Raw logging added inside the custom component (A2) | Needs C++ changes and can only show the parser's processed view. The built-in debugger shows true wire bytes. |
| `uart.button` / `uart.switch` platforms (B2) | Send fixed predefined sequences. The sequences are not known yet. |
| A `send_command` method with checksum computation (B3) | Premature. The checksum algorithm is unknown, and byte-for-byte replay does not need it. |
| Cracking the checksum algorithm | Not required for replay. Only required for synthesis, which is out of scope. |
| Rewiring or a second RJ45 socket | Unnecessary. RS-485 is multi-drop and both sockets sit on the same pair. The panel's frames already arrive at GPIO16. |
| Decoding newly discovered frames into sensors | Cannot be specified before the data exists. Out of scope. |
| Automatic source switching and Home Assistant automation logic | Explicitly the user's own follow-on work. Out of scope. |

---

## 7. Prerequisites

There are none.

No preparatory work ships value independently here. The change is self-contained: it is a
configuration change to `rain-director.yaml` plus logger settings, validated the same way as any
other change in this project, by `esphome config`, then `esphome compile`, then a flash to real
hardware and a check in Home Assistant.

---

## 8. Open Questions for the Design Phase

1. **Gating approach.** A, B or C from section 4. B is recommended, because it allows toggling from
   the dashboard with no reflash and produces no global DEBUG spam. The final choice was
   deliberately left to design.
2. **How to frame the debug output.** The `after:` block offers `bytes`, `timeout` and `delimiter`.
   Rain Director frames start with `<` rather than ending with a fixed delimiter, so a delimiter
   split may break frames in the wrong place. A timeout split may be the better fit. Design should
   decide, and should consider whether one setting can serve both hex frames and JSON.
3. **Hex or string output.** The payload mixes hex frames and JSON text. `log_hex` makes the frame
   bytes exact but makes the JSON unreadable. `log_string` does the reverse. Design should decide,
   and should consider logging both.
4. **Replay action input format.** Whether the Home Assistant action should take a hex string,
   which Pete can copy straight out of the log, or a raw byte array.
5. **The log line limit.** ESPHome limits a single hex log line to about 159 bytes. Design should
   decide how to handle output longer than that, whether by splitting frames more aggressively or
   by accepting truncation.

---

## 9. Relevant Existing Code and Integration Points

### Custom component

| Path | Lines | What |
|---|---|---|
| `components/rain_director/rain_director.cpp` | 66-96 | `MODE_MAPPINGS` table, including `0x04` at line 92 |
| `components/rain_director/rain_director.cpp` | 112-149 | `loop()`, byte reading, frame splitting, 500-character discard at line 144 |
| `components/rain_director/rain_director.cpp` | 151-168 | `process_buffer_()`, routing |
| `components/rain_director/rain_director.cpp` | 170-309 | `process_hex_code_()`, silent discards at lines 193-201, `1053` handling at line 208, no final else branch |
| `components/rain_director/rain_director.cpp` | 311-327 | `process_json_()`, reads only `"top"` and `"state"` |
| `components/rain_director/rain_director.cpp` | 329-339 | `extract_json_int_()` |
| `components/rain_director/rain_director.h` | — | Class definition, `last_status_byte_` |

None of these files need to change for this feature.

### User configuration

| Path | Lines | What |
|---|---|---|
| `rain-director.yaml` | 19-20 | `logger:` block, currently `level: INFO` |
| `rain-director.yaml` | 39-43 | `uart:` block, `id: uart_bus`, GPIO16 RX, GPIO17 TX, 9600 baud. The `debug:` block goes here. |
| `rain-director.yaml` | 65 | `water_source` text sensor |
| `rain-director.yaml` | 88-108 | Consumption tracking lambda, with the unknown-state hole at lines 99-106 |

### ESPHome framework, in the project venv

Base path: `venv/lib/python3.13/site-packages/esphome/components/`

| Path | Around line | What |
|---|---|---|
| `uart/uart_debugger.h` | — | `UARTDebugger` class, describes itself as a protocol reverse-engineering aid |
| `uart/uart_debugger.cpp` | 113, 157, 175, 195 | `log_hex`, `log_string`, `log_int`, `log_binary`, all at `ESP_LOGD` |
| `uart/uart_component_esp_idf.cpp` | 335 | `read_array()` fires the debug callback per byte |
| `uart/uart_component.h` | 57 | `UARTDevice::read()` routes through `read_array()` |
| `uart/__init__.py` | 501 | `uart.write` accepts a templatable payload |
| `uart/automation.h` | 11 | `UARTWriteAction::set_data_template` |
| `api/__init__.py` | 332 | `services:` renamed to `actions:` in this version |

ESPHome version in the project venv: 2025.12.5.

---

## 10. Follow-on Work

### A hard information gate

A follow-on research cycle will be needed once real capture data exists. The content of any
decoding feature or command-sending feature cannot be specified until the protocol is actually
observed. There is no way to write those requirements in advance, because nobody knows yet what the
panel sends, or whether it sends anything distinct at all.

This feature exists to open that gate. It is the only thing standing between the project and real
protocol data.

### Things the capture will reveal at no extra cost

- **The `tanklevels` plural hypothesis.** The JSON may already carry the underground rainwater tank
  level alongside `"top"`. If it does, it is being discarded today, and exposing it would be a
  small change that directly serves Pete's dry-spell problem.
- **What devices `30` and `40` are.** Both are on the bus and both are entirely discarded.
- **Whether `<10xx` frames other than `1053` exist.** If the panel emits a command frame, this is
  where it will appear.
- **The checksum algorithm.** Enough captured frames may make the algorithm obvious. That would
  enable synthesis, which is out of scope here but is the natural next step if replay alone proves
  insufficient.

### A separate defect worth fixing

The consumption tracking lambda in `rain-director.yaml` (lines 99-106) silently drops litres when
`water_source` is `"Unknown"`. Water is consumed but counted against neither the rainwater total
nor the mains total. This is a data loss defect independent of protocol discovery, and it should be
raised as its own piece of work.
