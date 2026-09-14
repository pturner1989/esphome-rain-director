# Specification: Protocol Discovery

**Version:** 1.2
**Date:** 2026-09-14
**Status:** Draft

---

## Problem Statement

Nobody has ever seen the full serial stream that the Rain Director puts on the RS-485 bus. The firmware keeps four values and silently discards everything else, including every frame from devices 20, 30 and 40, every frame that is not `2053` or `1053`, all checksums, and most of the JSON fields. Because no capture exists, no command feature can be specified: there is no observed data to design against.

## Beneficiaries

**Primary:**
- The operator who wants to press a button on the Rain Director control panel, see the bytes that appear on the bus, and send those same bytes back to reproduce the effect.

**Secondary:**
- Contributors who are reverse engineering new mode codes to submit as a pull request, and who currently have no way to see the frames the firmware throws away.
- Every user of the published repository, who keeps the component they have today because the new capability stays silent until someone asks for it.

---

## Outcomes

**Must Haves**
- The operator can see, in the ESPHome log, every byte the device reads from the bus and every byte the device sends onto it, without recompiling or reflashing the firmware.
- The operator can start and stop a capture session from the Home Assistant dashboard.
- The operator can send an arbitrary sequence of bytes back onto the bus on demand.
- A user who never switches capture on sees no change in log output and no change in behaviour compared to today.
- Writing to the bus is only possible during a deliberate discovery session, so a stray Home Assistant automation cannot write to a controller that operates mains water valves.

**Nice-to-haves**
- The captured stream reveals whether the underground tank level is already present in the JSON message and being discarded.
- The captured stream reveals what devices `30` and `40` are.
- Enough captured frames make the checksum algorithm obvious, which would enable later work that is out of scope here.

---

## Explicitly Out of Scope

- Decoding any newly discovered frames into sensors or text sensors.
- Computing or validating checksums.
- Synthesising new frames with altered parameters.
- Automatic or scheduled water source switching.
- Any Home Assistant automation logic built on top of the discovery.
- Changes to the existing mode mapping table.
- Fixing the consumption counting hole that drops litres while the water source is "Unknown". The research records this as a separate defect.

---

## Functional Requirements

**FR-01: Runtime Capture Control**
- **Statement:** The system shall present a control in Home Assistant that the operator can switch on and off at any time to start and stop raw bus capture, without recompiling or reflashing the firmware.

**FR-02: Capture Off After Restart**
- **Statement:** When the device starts, the system shall set the capture control to off, whatever its value was before the restart.

**FR-03: Received Bytes Are Visible**
- **Statement:** While capture is on, the system shall write every byte it receives from the bus to the ESPHome log, including bytes from frames the system does not otherwise report as a sensor value.

**FR-04: Transmitted Bytes Are Visible**
- **Statement:** While capture is on, the system shall write every byte it sends onto the bus to the ESPHome log.

**FR-05: Direction Is Distinguishable**
- **Statement:** While capture is on, the system shall mark each logged group of bytes so that the operator can tell whether the system received those bytes or sent them.

**FR-06: Silent When Capture Is Off**
- **Statement:** While capture is off, the system shall write no bus content to the ESPHome log.

**FR-07: Replay Of Supplied Bytes**
- **Statement:** While capture is on, when the operator triggers the replay action from Home Assistant with a byte sequence, the system shall send that byte sequence onto the bus unchanged.

**FR-08: Replay Refused While Capture Is Off**
- **Statement:** If the operator triggers the replay action while capture is off, then the system shall send nothing onto the bus and shall log a warning that says replay needs capture to be on.

**FR-09: Malformed Replay Input**
- **Statement:** While capture is on, if the operator triggers the replay action with input the system cannot read as a byte sequence, then the system shall send nothing onto the bus, shall log a warning naming the rejected input, and shall keep running.

**FR-10: Monitoring Is Unaffected By Capture**
- **Statement:** While capture is on, the system shall keep publishing tank level, mode, status, source and mode code, and the published tank level and mode shall match what the Rain Director control panel shows.

**FR-11: Captured Frames Can Be Replayed Without Editing**
- **Statement:** While capture is on, when the operator triggers the replay action with a byte sequence copied directly from a capture log line, the system shall send it onto the bus without the operator having to reformat it.

---

## Non-Functional Requirements

**NFR-01: No Change For Users Who Leave Capture Off**
- **Target:** With capture off, over a 10-minute window, the Home Assistant entity list differs from the released version only by the capture control, and the replay action is the only action added.
- **Verification:** platform-observed
- **Observable:** The Home Assistant entity list for a device that has never had capture switched on, compared against the released version, and the action list in Home Assistant Developer Tools for that device, both read over a 10-minute window.

---

## Acceptance Tests

**AT-01:**
- **Given:** a freshly flashed ESP32 running the firmware, connected to the Rain Director bus
- **When:** the operator opens the Rain Director device page in Home Assistant after the device comes online
- **Then:** the capture control is listed and shows as off

**AT-02:**
- **Given:** the device is online and capture is off
- **When:** the operator switches capture on in Home Assistant, walks to the Rain Director panel, presses a button, and reads the ESPHome log viewer
- **Then:** the log shows one or more lines of bus bytes marked as received, timestamped within a few seconds of the button press

**AT-03:**
- **Given:** capture is on and the operator has a byte sequence they have chosen by hand
- **When:** the operator triggers the replay action from Home Assistant Developer Tools with that byte sequence and reads the ESPHome log viewer
- **Then:** the log shows the same byte sequence marked as sent

**AT-04:**
- **Given:** the device has been running for at least 10 minutes with capture on, and the operator has noted the mode and level shown on the Rain Director control panel at the start and end of the period
- **When:** the operator reads the Rain Director entity history in Home Assistant for that period
- **Then:** all five values have kept updating, and the published tank level and mode match what the panel showed at the start and end of the period

**AT-05:**
- **Given:** capture is on and bus content is appearing in the log
- **When:** the operator switches capture off and watches the ESPHome log viewer for at least two minutes
- **Then:** no further bus content appears in the log, and the Rain Director entities keep updating in Home Assistant

**AT-06:**
- **Given:** capture is off
- **When:** the operator triggers the replay action from Home Assistant Developer Tools with a byte sequence copied from an earlier capture, and reads the ESPHome log viewer
- **Then:** the log shows a warning that replay needs capture to be on; the operator then switches capture on, triggers the same replay, and the log shows those bytes marked as sent

**AT-07:**
- **Given:** capture is on
- **When:** the operator triggers the replay action from Home Assistant Developer Tools with the input `hello`, which is not a valid byte sequence, and reads the ESPHome log viewer
- **Then:** the log shows a warning naming `hello` as the rejected input, shows no sent bytes, and the device stays online with its entities still updating

**AT-08:**
- **Given:** capture is on and bus content is appearing in the log
- **When:** the operator restarts the device, opens the Rain Director device page in Home Assistant, and reads the ESPHome log viewer
- **Then:** the capture control shows as off and no bus content appears in the ESPHome log viewer

**AT-09:**
- **Given:** capture is on and a frame is visible in the ESPHome log
- **When:** the operator copies that frame's bytes from the ESPHome log viewer and pastes them into the replay action in Home Assistant Developer Tools without editing them
- **Then:** the log shows those same bytes marked as sent

---

## Open Questions

- Does the display panel emit a distinct command frame when a person presses a button, or does it only report its resulting state? This is the single most important unknown, and the capture is the only way to answer it.

---

## Appendix

### Glossary

- **Bus:** The shared RS-485 wiring that connects the Rain Director devices. Every device on it sees every frame that any other device sends.
- **Frame:** One complete message on the bus. Rain Director frames start with `<`.
- **Device ID:** The first two characters of a frame, identifying the sender. Known senders are `10` the display panel, `20` the level sensor, and `30` and `40`, which are unidentified.
- **Byte sequence:** The byte form the capture log shows, which is the same form the replay action accepts. The operator can copy one from the log and paste it into the other without changing it.
- **Capture:** Writing the raw bytes on the bus to the ESPHome log so a person can read them.
- **Capture session:** The period between the operator switching capture on and switching it back off.
- **Replay:** Sending back the exact bytes that were captured, unchanged.
- **Synthesis:** Building a new frame with different values. This needs a valid checksum, so it is out of scope.
- **Checksum:** Trailing bytes in a frame that let the receiver detect corruption. The algorithm is unknown, and replay does not need it, because the captured bytes already carry a valid checksum.
- **Operator:** Anyone who has installed this firmware and can reach the Home Assistant dashboard and the ESPHome log viewer.
- **Contributor:** Someone reverse engineering new mode codes to submit as a pull request. They use the same channels as the operator.

### Assumptions and Dependencies

- The ESP32 already sits on the shared bus and already receives frames from all four device IDs.
- The existing wiring already allows the device to transmit onto the bus, so no rewiring is needed. The transmit pin is already connected to the transceiver's data input and the transceiver switches direction on its own.
- While the device is transmitting a replayed frame, it cannot receive. Any traffic another device puts on the bus during that brief window is not captured. This is a property of the half-duplex wiring, not of the capture.
- Runtime switching means the capture path is present even while capture is off. The small continuous cost of checking it is accepted. "No change in behaviour" means no change the operator can observe in the log or in Home Assistant entity values.
- Capture stays on until the operator switches it off or the device restarts. There is no automatic timeout. The operator is trusted to end a discovery session, and a restart is the backstop.
- There is no vendor documentation for this protocol and no prior art. Everything about the protocol must be learned from the bus.
- Two devices transmitting at once can corrupt each other. The operator accepts this risk. The worst expected result is a garbled frame and a retry.
- It is unknown whether the controller accepts a frame that claims to be device `10` while the real display panel is also on the bus.
- The design chooses how captured bytes are represented in the log. Whatever it chooses, the replay action must accept that same representation unchanged, so the operator can copy a frame from the log and paste it straight back.
- Validation is manual. This project has no automated tests. Every acceptance test is a sequence of steps a person performs on real hardware.

### References

- Research findings: `/home/pete/dev/esphome-rain-director/.sdd/protocol-discovery/research.md`
- Init and Backup Modes specification: `/home/pete/dev/esphome-rain-director/.sdd/init-backup-modes/specification.md`
- Initial Release specification: `/home/pete/dev/esphome-rain-director/.sdd/initial-release/specification.md`
- Project guidelines: `/home/pete/dev/esphome-rain-director/.sdd/project-guidelines.md`

### Change History
| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-09-14 | Pete | Initial specification |
| 1.1 | 2026-09-14 | Pete | Applied specification review findings |
| 1.2 | 2026-09-14 | Pete | Applied second specification review findings |
</content>
</invoke>
