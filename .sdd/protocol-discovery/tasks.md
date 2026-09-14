# Tasks: Protocol Discovery

**Linked Design:** `.sdd/protocol-discovery/design.md`
**Linked Specification:** `.sdd/protocol-discovery/specification.md`

### Task 1: Bus Capture switch and two-way hex logging

- **Status:** Done
- **Blocked by:** None

**What to build:**

Add a Bus Capture switch that appears in Home Assistant as a configuration entity and is off after every restart, and attach a debug block to the existing UART bus that watches both directions, groups bytes on an idle gap and writes each group to the log at INFO level as uppercase hexadecimal byte pairs. Give received groups and sent groups different tags, so the operator can tell a line the device read from a line the device wrote. The sequence checks the switch first and returns at once while the switch is off, so the log stays free of bus content until the operator asks for a capture. After this task an operator can switch capture on, watch real Rain Director traffic scroll past in the ESPHome log viewer, switch it off and see the log go quiet, and restart the device to find capture off again. The five existing Rain Director values keep updating throughout, because the component itself is not touched.

**Acceptance criteria:**

**AC-1:**
- **Given:** the firmware is built with `esphome config` and `esphome compile`, flashed to the ESP32, and the device has come online
- **When:** the operator opens the Rain Director device page in Home Assistant
- **Then:** a Bus Capture control is listed under configuration and shows as off

**AC-2:**
- **Given:** the device is online and Bus Capture is off
- **When:** the operator watches the ESPHome log viewer for two minutes without touching the switch
- **Then:** no bus bytes appear in the log, and the only new entity compared with the released firmware is the Bus Capture control

**AC-3:**
- **Given:** the device is online and connected to the Rain Director bus
- **When:** the operator switches Bus Capture on and reads the ESPHome log viewer
- **Then:** lines of uppercase two-digit hexadecimal byte pairs appear, each tagged as received, within a few seconds

**AC-4:**
- **Given:** Bus Capture is on and the operator walks to the Rain Director panel and presses a button
- **When:** the operator reads the ESPHome log viewer
- **Then:** one or more received lines carry bytes timestamped within a few seconds of the button press, including frames that do not correspond to any published sensor value

**AC-5:**
- **Given:** Bus Capture is on and bus content is appearing in the log
- **When:** the operator switches Bus Capture off and watches the log viewer for at least two minutes
- **Then:** no further bus content appears, and the Rain Director entities keep updating in Home Assistant

**AC-6:**
- **Given:** Bus Capture is on and bus content is appearing in the log
- **When:** the operator restarts the device, then opens the device page and the log viewer
- **Then:** the Bus Capture control shows as off and no bus content appears in the log

**AC-7:**
- **Given:** the device has run for at least ten minutes with Bus Capture on, and the operator noted the mode and level on the Rain Director panel at the start and the end of that period
- **When:** the operator reads the Rain Director entity history in Home Assistant for that period
- **Then:** tank level, mode, status, source and mode code have all kept updating, and the published tank level and mode match what the panel showed at the start and the end

**Notes:**

The sequence must log at INFO, because the project ships the logger at INFO and raising the global level would fill the log with output from every other component. The lambda must call a 10 ms blocking delay after its log call, matching the built-in hex helper, or the ESPHome logger drops API log lines during exactly the bursts of traffic the operator most wants to read. Group bytes on an idle gap with a byte cap rather than on a delimiter, because frames start with the delimiter instead of ending with it. Nothing writes to the bus until the replay action lands in Task 3, so during this task the sent tag produces no lines and the operator sees received lines only, which is expected and not a fault. Validate with `esphome config`, then `esphome compile`, then flash and observe; there is no automated test suite in this project.

---

### Task 2: Printable-text line beside each hex line

- **Status:** Backlog
- **Blocked by:** Task 1

**What to build:**

Extend the capture sequence so each group of bytes produces a second log line that shows the same bytes as printable characters, with a dot in place of any byte that is not printable, tagged so it is clearly the text view of the received group. The Rain Director frames carry a JSON message, and the operator needs to read that message by eye without converting hexadecimal by hand. After this task a capture shows every group twice: once as the replayable hexadecimal and once as human-readable text.

**Acceptance criteria:**

**AC-1:**
- **Given:** the rebuilt firmware is flashed and Bus Capture is on
- **When:** the operator reads the ESPHome log viewer
- **Then:** each received hexadecimal line is followed by a text line with a different tag, holding the same number of characters as the hexadecimal line holds byte pairs

**AC-2:**
- **Given:** Bus Capture is on and a frame carrying the JSON message has been captured
- **When:** the operator reads the text line for that frame
- **Then:** the JSON text is readable as characters, and any byte that is not printable shows as a dot

**AC-3:**
- **Given:** Bus Capture is off
- **When:** the operator watches the log viewer for two minutes
- **Then:** neither hexadecimal lines nor text lines appear

**Notes:**

The text line is for reading only and the replay action never accepts it, so keep the hexadecimal line first and give the text line its own tag. This second log call needs its own 10 ms delay, which doubles the main-loop cost of capture; the design accepts that and names this line the first thing to remove if log timing causes trouble on real hardware. No requirement depends on this line, so if the operator sees dropped log lines under heavy traffic, removing it is a safe retreat.

---

### Task 3: Replay action happy path

- **Status:** Backlog
- **Blocked by:** Task 1

**What to build:**

Add a Home Assistant action named `replay_bytes` that takes one text field of hexadecimal byte pairs, checks that Bus Capture is on, parses the text into bytes under the strict rules, and writes those bytes to the bus. The capture check returns silently for now: when the switch is off the action does nothing and logs nothing, and Task 4 gives that check its warning message and its fixed position ahead of input parsing. Accept hexadecimal digit pairs in either case with one or more separators between pairs, where a separator is a space, a colon, a comma or a hyphen. Reject anything else, and for now log a plain warning on rejection; Task 5 makes those refusals precise. After this task the operator can call the action from Home Assistant Developer Tools with a byte sequence they chose and see those exact bytes appear in the capture log marked as sent, which is the first time the device has ever written to the bus.

**Acceptance criteria:**

**AC-1:**
- **Given:** the rebuilt firmware is flashed and the device is online
- **When:** the operator opens Home Assistant Developer Tools and lists every action the device offers
- **Then:** a `replay_bytes` action is listed with a single text field named `hex`, and the only action the device offers that the released firmware did not is `replay_bytes`

**AC-2:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with a byte sequence of their own choosing, written as hexadecimal pairs separated by spaces, and reads the ESPHome log viewer
- **Then:** the log shows a line marked as sent carrying exactly those bytes, in the same order

**AC-3:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with the same bytes written with colons, with commas and with hyphens as separators, and in lower case
- **Then:** each call produces a sent line carrying the identical byte sequence

**AC-4:**
- **Given:** Bus Capture is on and a replay has just been sent
- **When:** the operator reads the lines that follow the sent line and compares them with it byte for byte
- **Then:** the operator records whether a received line with content identical to the sent line follows it, and records that if a Rain Director entity value moves at that moment, the device has parsed its own echo rather than a reply from the controller

**Notes:**

Cap the parsed result at 256 bytes, above the capture group size and above any frame the component accepts. The transceiver is auto-direction with no receiver-enable line, so the device will probably read its own transmitted bytes straight back and log them as a received line with identical content; this is expected, and it is not evidence that the controller replied. A replayed frame beginning `<2053` or `<1053` may therefore be parsed as a genuine frame and briefly move the tank level or mode, which the next real frame corrects, so do not read such a change as a response. Choose replay bytes carefully for the first test, because the bus operates mains water valves.

---

### Task 4: Replay refused while capture is off

- **Status:** Backlog
- **Blocked by:** Task 3

**What to build:**

Give the silent capture check from Task 3 a warning message and a fixed position ahead of input parsing. The action reads the Bus Capture switch before it looks at the input at all, and when the switch is off it writes nothing to the bus and logs a warning that says replay needs capture to be on. Checking the switch first means an operator who simply forgot the switch is told about the switch rather than about their input. After this task a stray Home Assistant automation cannot put bytes on a bus that operates mains water valves outside a deliberate discovery session, and the operator sees a clear reason when a call does nothing.

**Acceptance criteria:**

**AC-1:**
- **Given:** the rebuilt firmware is flashed and Bus Capture is off
- **When:** the operator calls `replay_bytes` with a valid byte sequence copied from an earlier capture, and reads the ESPHome log viewer
- **Then:** the log shows a warning saying replay needs capture to be on

**AC-2:**
- **Given:** the operator has just seen that warning
- **When:** the operator switches Bus Capture on and calls `replay_bytes` again with the same byte sequence
- **Then:** the log shows those bytes marked as sent

**AC-3:**
- **Given:** Bus Capture is off
- **When:** the operator calls `replay_bytes` with input that is not a valid byte sequence
- **Then:** the warning names the capture switch, not the input

**Notes:**

The switch is the only shared state between capture and replay, so one control opens and closes a whole discovery session. The design fixes the order of the two checks, and AC-3 is what proves the order on hardware. A missing sent line is not evidence that the action refused, because capture being off suppresses that line anyway; the evidence is that the capture check runs before any write, which AC-3 shows by the wording of the warning. The action name and the field name are published configuration: once released, renaming either breaks any saved Home Assistant call, so fix both now.

---

### Task 5: Malformed replay input rejected

- **Status:** Backlog
- **Blocked by:** Task 4

**What to build:**

Make the replay parser reject input it cannot read as a byte sequence, write nothing to the bus, log a warning quoting the input exactly as the operator typed it, and leave the device running. Reject the input when any character is neither a hexadecimal digit nor one of the four separators, when the count of hexadecimal digits is odd, when no bytes remain, and when the result would be more than 256 bytes. The parser must never skip a character it does not recognise. After this task the operator can mistype a frame and get a warning that names what they typed, while the device carries on monitoring as if nothing happened.

**Acceptance criteria:**

**AC-1:**
- **Given:** the rebuilt firmware is flashed and Bus Capture is on
- **When:** the operator calls `replay_bytes` with the input `hello` and reads the ESPHome log viewer
- **Then:** the log shows a warning naming `hello` as the rejected input, no sent line appears, and the device stays online with its entities still updating

**AC-2:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with an odd number of hexadecimal digits, such as `3C 31 3`
- **Then:** the log shows a warning quoting that input and no bytes are sent

**AC-3:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with input that mixes valid pairs and stray characters, such as `3C 31 zz 30`
- **Then:** the log shows a warning quoting that input, and no sent line appears at all, so no partial byte sequence reaches the bus

**AC-4:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with an empty field
- **Then:** the log shows a warning and no sent line appears

**AC-5:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with more than 256 bytes of valid pairs
- **Then:** the log shows a warning stating the 256-byte limit and no sent line appears

**Notes:**

AC-3 is the important one. A lenient parser that ignores what it does not recognise would read a log timestamp or a line number as hexadecimal digits and write bytes that were never on the wire to a controller that operates mains water valves. Rejecting the whole input is the only safe behaviour, so do not send the pairs that did parse. The device must keep monitoring after every rejection, which AC-1 checks by reading the entity list after the warning. This task does not depend on Task 4's behaviour, but both tasks change the same action body, so doing them in order avoids editing the same lines twice.

---

### Task 6: Paste a whole capture log line

- **Status:** Backlog
- **Blocked by:** Task 5

**What to build:**

Let the operator paste a complete line copied from the ESPHome log viewer into the replay field without editing it. Before the strict rules run, trim leading and trailing whitespace from the input, then, if the input holds the sequence made of a closing square bracket, a colon and a space, discard everything up to and including the last occurrence of that sequence and apply the strict rules to what remains. Nothing else is stripped. After this task the operator can select a captured line in the log viewer, copy it with its timestamp and tag, paste it straight into Developer Tools, and see the same bytes go out on the bus.

**Acceptance criteria:**

**AC-1:**
- **Given:** the rebuilt firmware is flashed, Bus Capture is on, and a received hexadecimal line is visible in the log viewer
- **When:** the operator copies that whole line, prefix included, pastes it into the `replay_bytes` field without editing it, and calls the action
- **Then:** the log shows a sent line carrying exactly the bytes from the copied line

**AC-2:**
- **Given:** Bus Capture is on
- **When:** the operator calls `replay_bytes` with a bare byte sequence that has a trailing space and a trailing newline
- **Then:** the bytes are sent unchanged and no warning appears

**AC-3:**
- **Given:** Bus Capture is on
- **When:** the operator pastes a copied text line rather than a copied hexadecimal line
- **Then:** the log shows a warning quoting the input and nothing is sent

**AC-4:**
- **Given:** Bus Capture is on
- **When:** the operator pastes a log line whose byte portion holds a stray character, such as a partly selected line
- **Then:** the log shows a warning quoting the input and nothing is sent

**Notes:**

The closing bracket is neither a hexadecimal digit nor a separator, so the sequence can never appear inside the byte portion of a line. That makes the last occurrence well defined and means nothing is stripped from a bare byte sequence. AC-3 and AC-4 together confirm that the prefix strip did not turn the parser lenient: the only relaxations allowed are the outer whitespace trim and this one prefix strip. Expect the replayed frame to appear again as a received line right afterwards, because the device reads back its own transmission.

---

### Task 7: Document capture and replay in the README

- **Status:** Backlog
- **Blocked by:** Task 2, Task 4, Task 6

**What to build:**

Add a section to the README covering the whole capture and replay workflow, because the README is the only user documentation for a published repository and says nothing about either feature today. Explain what the Bus Capture switch is, that it is off after every restart, and that a user who never switches it on sees no change. Explain how to read a capture line: which tag means received and which means sent, that the hexadecimal line is the one to copy and the text line is for reading only, and that a whole log line can be pasted with its prefix. Explain how to call the replay action from Developer Tools with a worked example, and state the warnings plainly.

**Acceptance criteria:**

**AC-1:**
- **Given:** the README change is written
- **When:** the author follows the new section word for word on the device, without opening the design document or the configuration file
- **Then:** the author switches capture on, finds a received line in the log viewer, copies it, replays it and switches capture off, using only the steps the section gives

**AC-2:**
- **Given:** the README change is written
- **When:** a reader reads the warnings in the new section
- **Then:** the section states that replay writes to a controller that operates mains water valves, that a replayed frame can collide with another frame in flight, that capture should be switched off at the end of a session, and that replayed bytes will probably appear back in the capture as a received line where they can briefly move a sensor value and are no proof of a reply

**AC-3:**
- **Given:** the README change is written
- **When:** a reader copies the worked replay example from the README into Developer Tools with capture on
- **Then:** the example is accepted and the log shows a sent line

**Notes:**

Keep the section short and aimed at an intermediate ESPHome user, matching the rest of the README. Use the entity name and the action name exactly as the firmware publishes them, since both are fixed configuration that saved Home Assistant calls depend on. The echo warning matters most: without it a reader will read a sensor value that moves right after a replay as evidence that the controller responded, which is the one open question the capture exists to answer.
