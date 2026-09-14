# Design: Protocol Discovery

**Version:** 1.2
**Date:** 2026-09-14
**Status:** Draft
**Linked Specification** `.sdd/protocol-discovery/specification.md`

## Architecture Overview

### Current Architecture Context

The ESP32 sits on the RS-485 bus through a MAX485 auto-direction transceiver, the `uart_bus` UART component
at 9600 baud moves bytes both ways, the `rain_director` custom component drains the port each `loop()` and
publishes five values while dropping the rest, the firmware never writes to the bus, and the logger is INFO.

### Proposed Architecture

The design adds two things to `rain-director.yaml` and changes nothing else; the C++ component is not
touched. The first is a `debug:` block on the existing `uart:` entry. ESPHome's UART debugger fires a
callback from inside `read_array()` and `write_array()`, and `rain_director` already drains the port each
loop, so the debugger sees the whole stream both ways, including frames the parser discards. It gathers
bytes into a chunk and runs a `sequence:` — here a lambda that logs at INFO only while a runtime switch is
on, in place of the default DEBUG-level `UARTDebug::log_hex` action.

The second is a user-defined Home Assistant action under `api:` → `actions:`. It takes a text field, checks
the same switch, parses the text into bytes, and writes them to `uart_bus`. The switch is the only shared
state, so one control starts and stops a whole discovery session.

```
Rain Director bus --(RS-485)-- MAX485 -- uart_bus --> rain_director component (unchanged)
                                            |
                                            +--> debug: sequence -> [switch on?] -> INFO log
                                            ^
Home Assistant --> action replay_bytes -> [switch on?] -> parse hex -> write_array
```

### Technology Decisions

**Frame alignment.** The chunk boundary comes from an idle gap, not a byte pattern: `after: { timeout:
100ms, bytes: 150 }` and no `delimiter`. A delimiter split is wrong twice over: frames start with `<` rather
than end with it, so each frame's start would land on the previous line, and the JSON text holds no `<`. At
9600 baud a 100 ms gap is about 96 byte-times of silence: far more than the gaps inside a frame, far less
than the pause between frames. The 150-byte cap keeps each line inside the log buffer.

**Byte representation.** The capture logs each chunk twice. The line tagged `rd.rx` or `rd.tx` carries the
replayable form: uppercase two-digit hexadecimal, one space between bytes, nothing else. It round-trips
every byte, including terminators and any non-printable checksum byte a text form would lose. The line
tagged `rd.rx.text` or `rd.tx.text` carries the same chunk as printable characters, a dot for any other
byte, because the JSON message is what the operator most needs to read by eye. That second line doubles the
capture's main-loop cost, since each log call carries its own 10 ms delay; the Risks below price it and name
it the first thing to drop. Replay never accepts the text line.

**Direction marking.** The direction appears in the log tag, not the message body: `rd.rx` for bytes read
from the bus, `rd.tx` for bytes written to it. This keeps the body as pure hexadecimal and lets the operator
filter by direction. The tags are short on purpose: a longer tag takes space from the usable hex line.

**Replay input validation.** The action takes one string. The parser first trims leading and trailing
whitespace: space, tab, carriage return and newline. A paste from the log viewer or into the Home Assistant
field often picks up a trailing space or newline, and FR-11 and AT-09 promise that such a paste works. Next
the parser strips an optional ESPHome log prefix: if the input holds the sequence `]: `, it discards
everything up to and including the last occurrence of it and applies the strict rules to what remains. `]`
is neither a hexadecimal digit nor a separator, so `]: ` can never appear inside the byte portion of a line;
that makes the last occurrence well defined, and nothing is stripped from bare bytes with no prefix. The
strict rules accept hexadecimal digit pairs in either case, and one or more separators between pairs but
never inside a pair: space, colon, comma or hyphen. They reject the input if the digit count is odd, if no
bytes remain, if the result exceeds 256 bytes, or if any remaining character is neither a hexadecimal digit
nor one of those four separators. The parser skips no unrecognised character. An
implementer must not write a lenient parser that ignores what it does not recognise: it would read a log
prefix's timestamp and line number as hexadecimal digits and write bytes that were never on the wire to a
bus that operates mains water valves. On rejection the action writes nothing and logs a warning quoting the
input as typed. The 256-byte cap sits above the debugger's 150-byte chunk and above any frame the component
accepts.

**Gating.** ESPHome has no config-level conditional, so a substitution cannot remove the debug block from
the build. A runtime switch is the only control that works without a reflash.

### Quality Attributes

NFR-01 is met by an architectural choice, not by a component. The capture path is compiled in and the
debugger callback runs for every byte either way, but the guard at the top of the sequence lambda rejects
the chunk before anything reaches the log. So with capture off the log carries no bus content and the device
gains one entity and one action. The cost is not zero: per byte, one `std::function` dispatch, one
`millis()` call and one buffer append; per chunk, one rejected automation dispatch. Assumptions accept it.

FR-10 is met by not changing the component: `components/rain_director/` reads, parses and publishes exactly
as today, the debugger only observes, and capture itself changes no published value. Replay is different: it
puts bytes on the bus, which opens two interactions recorded in the Risks below, the half-duplex window and
the echo of the device's own transmitted bytes. The work also sits in one file, so maintainability holds.

---

## API Design

**Action:** `replay_bytes`, callable from Home Assistant Developer Tools. **Input:** one field, `hex`, a
string of hexadecimal byte pairs with optional space, colon, comma or hyphen separators, for example
`3C 31 30 35 33 30 30 30 30 38 30`. **Output:** none; the result is visible in the ESPHome log.

**Behaviour and errors:**

| Condition | Result |
|---|---|
| Input holds the sequence `]: ` | If the input holds the sequence `]: `, the parser discards everything up to and including the last occurrence of it and applies the strict rules to what remains. |
| Bus Capture is off | Nothing is written. A warning says replay needs capture to be on. |
| Input holds a character that is not a hexadecimal digit or a separator | Nothing is written. A warning quotes the input. |
| Input has an odd number of hexadecimal digits, or yields no bytes | Nothing is written. A warning quotes the input. |
| Input yields more than 256 bytes | Nothing is written. A warning states the limit. |

The capture check runs before the parse check, so an operator who forgets the switch is told about the
switch. The action and field names are published configuration: renaming either breaks a saved Home
Assistant call, so both stay fixed once released.

---

## Components

### Modified

#### `uart_bus` UART configuration

- **Change:** Today the `uart:` entry declares the port only. It gains a `debug:` block with `direction:
  BOTH`, an `after:` rule that splits on an idle gap, and a `sequence:` that replaces the default
  DEBUG-level hex action with an INFO-level lambda guarded by the capture switch. The lambda logs two lines
  per chunk at `ESP_LOGI`: line one the uppercase hexadecimal, the replayable form, line two the same bytes
  as printable text with a dot for any non-printable byte. After each of its two log calls it calls
  `delay(10)` — a 10 ms blocking delay, not `yield()` — matching `UARTDebug::log_hex`, because otherwise the
  ESPHome logger can drop API log lines.
- **Dependants:** None. The `rain_director` component keeps the same `uart_id` and is unaffected.
- **Kind:** ESPHome YAML configuration block.
- **Details:**
  ```yaml
  uart:
    id: uart_bus
    debug:
      direction: BOTH
      after: { bytes: 150, timeout: 100ms }
      sequence:
        - lambda: |-
            if (!id(bus_capture).state) return;
  ```
- **Rationale:** This block is the whole capture path. The callback sees every byte the port reads, so FR-03
  holds even for frames the parser drops, and every byte it writes, so FR-04 holds for replayed frames. The
  tag carries the direction, giving FR-05. The guard keeps bus content out of the log while the switch is
  off, giving FR-06 without lowering the global logger level. The hexadecimal line is what replay accepts
  unchanged, the capture half of FR-11.

### Added

#### `bus_capture` template switch

- **Responsibility:** Hold the runtime flag that says whether a discovery session is open.
- **Consumers:** The `uart_bus` debug sequence and the `replay_bytes` action.
- **Location:** `rain-director.yaml`, `switch:` block.
- **Kind:** ESPHome template switch entity.
- **Details:**
  ```yaml
  switch:
    - platform: template
      name: "Bus Capture"
      id: bus_capture
      icon: "mdi:bug-outline"
      entity_category: config
      optimistic: true
      restore_mode: ALWAYS_OFF
  ```
- **Rationale:** The switch is the control FR-01 asks for: it appears in Home Assistant, the operator turns
  it on and off at any time, and nothing needs reflashing. `ALWAYS_OFF` starts the device with capture off
  after every restart, which gives FR-02. Both guards read it, so FR-06 and FR-08 depend on it too.

#### `replay_bytes` API action

- **Responsibility:** Turn an operator's hexadecimal string into bytes and write them, or refuse and say why.
- **Consumers:** Home Assistant Developer Tools, and any Home Assistant script the operator writes.
- **Location:** `rain-director.yaml`, `api:` → `actions:`.
- **Kind:** ESPHome user-defined action with a lambda body.
- **Details:**
  ```yaml
  api:
    actions:
      - action: replay_bytes
        variables: { hex: string }
        then:
          - lambda: |-
  ```
- **Rationale:** This action is how the operator puts captured bytes back on the bus, which is FR-07. It
  reads the capture switch first and refuses with a warning when the switch is off, which is FR-08 and which
  stops a stray automation writing to a controller that operates mains water valves. It rejects input it
  cannot read as bytes, names it in the warning and keeps running, which is FR-09. It accepts a whole copied
  log line: if the input holds the sequence `]: `, the parser discards everything up to and including the
  last occurrence of it and applies the strict rules to what remains — exactly the hexadecimal form the
  capture logs, the replay half of FR-11. A lambda, not `uart.write`, gives the warnings somewhere to live.

### Used

#### `rain_director` custom component

- **Location:** `components/rain_director/`
- **Provides:** Monitoring, and a `loop()` that drains `uart_bus`, which puts the whole received stream in
  front of the debug callback.
- **Used by:** The `uart_bus` debug block, indirectly.
- **Rationale:** Not changed at all. FR-10 holds because the parsing and publishing path is untouched, so
  tank level, mode, status, source and mode code keep updating as today. Draining the port each loop is also
  why no `dummy_receiver` is needed.

#### Framework and configuration already present

- **ESPHome UART debugger** (`esphome/components/uart/uart_debugger.*`): the per-byte callback from
  `read_array()` and `write_array()`, the `after:` chunking rules, the direction passed to the sequence, and
  a recursion guard that stops logging inside the sequence re-triggering it.
- **ESPHome API server** (`api:` block): already connected; `actions:` publishes `replay_bytes` as a service.
- **ESPHome logger at INFO** (`logger:` block): left at INFO, so the capture is visible without raising the
  global level and without the extra DEBUG output every other component would add.

---

## Feasibility Review

There is no design blocker: the change sits in `rain-director.yaml`, uses framework features the research has
verified in this ESPHome version, and needs no C++ and no rewiring. Validation is manual: `esphome config`,
`esphome compile`, then a flash and a person reading the entity list and the log viewer.

---

## Risks and Dependencies

**Half-duplex transmission blocks reception.** While the device sends a replayed frame it cannot receive, so
traffic in that window is not captured. A twelve-byte frame takes about 13 ms at 9600 baud, so it is short.

**Bus collision on transmit.** The MAX485 is auto-direction and cannot sense the bus before writing, so a
replayed frame can corrupt one already in flight. The worst case is a garbled frame and a retry by the
sender. The user accepts this, and the capture switch limits it to a deliberate session.

**An accepted frame may have real effects.** Replay writes to a controller that operates mains water valves.
It refuses while the capture switch is off, so no automation writes by accident, but the operator still
chooses the bytes.

**Unknown protocol response.** Nobody knows whether the controller accepts a frame claiming to be device `10`
while the real panel is on the bus, or whether the panel emits a frame on a button press. The capture answers
both, and neither answer changes this design.

**Chunking is a heuristic.** If real traffic runs frames together with no idle gap, one log line may hold more
than one frame. The bytes are still all there in wire order, and a frame boundary shows in the hex line as the
byte `3C`, the `<` start delimiter, so the operator can copy one frame. Changing the timeout is a one-line edit.

**Log delays cost main-loop time.** The sequence calls `delay(10)` after each log call because the ESPHome
logger can drop API log lines otherwise, and dropped lines would break FR-03 where it matters most, a burst
of frames around a button press. At the worst-case continuous rate, 960 bytes per second over the 150-byte
chunk is about 6.4 chunks per second, two log lines each at 10 ms, so the delays add roughly 130 ms of
main-loop blocking per second. The ESP-IDF receive buffer holds 256 bytes, about 266 ms at this baud rate,
so this is survivable, but it is a stated decision. If log timing proves a problem on real hardware, the
printable-text line is the first to drop, because no requirement depends on it.

**The device will probably read back its own replayed bytes.** The transceiver is an auto-direction MAX485
module with no DE/RE control line. On most such modules the receiver stays enabled and only the driver is
switched, so transmitted bytes appear back on the device's own receive pin and the component's `loop()`
reads them like any other frame. We cannot inspect this module's schematic, so treat this as the expected
behaviour rather than a certainty; the first replay test settles it at once. Two consequences follow. First,
a replayed frame may be parsed as if the controller sent it: replay a frame beginning `<2053` or `<1053` and
the component parses the echo and publishes a tank level or mode the Rain Director never sent. This is
expected and transient, the next genuine frame corrects it, and the operator must not read a value that
changes right after a replay as evidence that the controller responded. Second, an echo can be mistaken for
a reply. It arrives immediately after a transmitted line with byte-for-byte identical content; a genuine
response would differ in content or arrive after a gap. This matters because whether the controller responds
at all is the one open question in the specification.

---

## Documentation

`README.md` is the project's only user documentation and it is published, so it needs a new section on capture
and replay, stating:

- What the Bus Capture switch is, that it is off after every restart, and that a user who never turns it on sees no change.
- How to read a capture line: which tag means received and which means sent, that the hexadecimal line is
  the one to copy and the text line is for reading only, and that the whole log line can be copied, prefix
  included, because if the input holds the sequence `]: `, the parser discards everything up to and
  including the last occurrence of it and applies the strict rules to what remains.
- How to call `replay_bytes` from Developer Tools, with a worked example and warnings: replay writes to a
  controller that operates mains water valves, a replayed frame can collide with another, capture should be
  switched off at the end of a session, and replayed bytes will probably appear back in the capture as a
  received line, where such an echo can briefly move a sensor value and is no proof of a reply.

---

## Appendix

### References

- Specification: `.sdd/protocol-discovery/specification.md`
- Research findings: `.sdd/protocol-discovery/research.md`
- Project guidelines: `.sdd/project-guidelines.md`
- ESPHome UART component documentation: https://esphome.io/components/uart.html

### Change History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-09-14 | Pete | Initial design. |
| 1.1 | 2026-09-14 | Pete | Applied design review findings. |
| 1.2 | 2026-09-14 | Pete | Applied second design review findings. |
