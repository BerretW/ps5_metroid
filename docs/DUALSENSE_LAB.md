# DualSense Lab

DualSense Lab is an interactive diagnostics screen inside PROJECT ECHO, not a separate executable. Open it from the main menu. The screen does not reset or modify the game simulation.

## Controls

| Action | DualSense | Windows keyboard |
| --- | --- | --- |
| Return to menu | Circle | Escape |
| Run short controller rumble | Cross | R |
| Change lightbar color (red, green, blue, white, off) | Square | L |
| Test trigger rumble, only if SDL reports support | L3 | T |
| Export the current report | Triangle | E |
| Confirm the last output test was physically observed | L1 | P |
| Mark the last output test as failed | R1 | F |
| Select another connected controller | D-pad left/right | Left/right arrows |
| Adjust radial stick dead zone (5%-40%) | D-pad up/down | Up/down arrows |

Rumble tests last at most 250 ms and are never started automatically. Leaving the lab, disconnecting the controller, or shutting down attempts to stop effects. A lightbar test cycles through explicit colors and black; leaving the lab attempts a black reset because SDL does not provide a query for the previous system-selected color. On Windows, press P to confirm an observed output or F to record a failed physical effect. After a successful rumble, trigger-rumble or LED test, its capability line reads `CHECK L1 OK / R1 FAIL` (Windows: `P OK / F FAIL`) until you answer; other actions in between do not discard the pending test. The line then shows `CONFIRMED` or `FAILED`, and the next export records it.

The button diagram lights for currently pressed buttons. Stick and trigger values are sampled live. Sticks show raw values, normalized values after the configurable radial dead zone, and observed axis extrema. Trigger values are shown raw and as a percentage. Battery is displayed only as the SDL-reported category; no percentage is invented.

Touch contacts are shown only when SDL reports touchpad finger slots. A touchpad click is a separate button. Sensors are enabled only while the lab is open and only when the controller advertises them. Gyroscope units are radians/second and accelerometer units are meters/second squared; raw gyro values do not determine absolute orientation.

## Export

Press Triangle (or E on Windows) to write a single JSON snapshot:

- Windows: `logs/dualsense_report.json`, relative to the application working directory.
- PS5: `/data/project_echo/logs/dualsense_report.json`.

The report includes SDL version, controller name and instance ID, battery category, current button and axis values, calibration extrema, touch samples, sensor values, enumerated audio device names, capability status, API availability, hardware-verification status and the last output-test result. It never records or stores microphone audio. A failed write is shown on screen and logged.

**SUPPORTED** means actual input/sensor data was observed, or the operator explicitly confirmed an output test. **UNSUPPORTED** means the connected backend reports no capability or the pinned PS5 backend has no implementation. **NOT TESTED** means no functional hardware observation has been recorded; a successful API call alone remains NOT TESTED. **FAILED** records a failed API call or an operator-confirmed failed output test.

Automated coverage uses simulated values and SDL's software/dummy renderer; it does not verify physical devices. Follow [the hardware checklist](DUALSENSE_HARDWARE_TESTS.md) and export a report from each target used.
