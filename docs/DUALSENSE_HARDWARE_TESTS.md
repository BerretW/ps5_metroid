# DualSense hardware checklist

Run this checklist on each platform/controller combination. Do not mark an item supported based only on a successful build or API return. For output tests, use L1 (P on Windows) to record a physically observed success or R1 (F on Windows) to record failure, then export the JSON report with Triangle (E on Windows). The report's hardware-dependent statuses remain NOT TESTED until observation is recorded.

## Input and connection

- [ ] Enter DualSense Lab from the main menu and return with Circle.
- [ ] Press Cross, Circle, Square, Triangle and Options; verify the matching diagram indicators.
- [ ] Press Create, PS and Mute; record whether the backend actually exposes each button.
- [ ] Press L1/R1, click L3/R3, use every d-pad direction.
- [ ] Move both sticks through their full ranges; inspect raw values, normalized values, center drift and recorded extrema.
- [ ] Adjust the radial dead zone in both directions and verify its displayed percentage and stick indicators change.
- [ ] Slowly press and release L2/R2; inspect raw values and percentage travel.
- [ ] Press the touchpad button and verify it is distinct from touch contact reporting.
- [ ] If SDL reports touch slots, touch with one finger and then two, move each finger and inspect coordinates/pressure. Otherwise leave the feature UNSUPPORTED or NOT TESTED as reported.
- [ ] Move and rotate the controller. If SDL advertises sensors, verify gyro and accelerometer values change with motion. Do not infer absolute orientation from gyro data alone.
- [ ] If more than one controller is connected, cycle device selection and verify its name and instance ID; disconnect the selected device, reconnect, and confirm the lab does not read a stale controller.

## Output and audio

- [ ] Start ordinary rumble with Cross/R and confirm it is brief (maximum 250 ms); mark L1 if physically felt or R1 if the API call succeeded but no vibration occurred.
- [ ] If the device advertises trigger rumble, run it with L3/T and confirm its effect separately from ordinary rumble. Do not interpret it as adaptive resistance.
- [ ] Cycle red, green, blue, white and off with Square/L; confirm each visible color and record the result. Leaving the lab attempts a black lightbar reset.
- [ ] Confirm unsupported adaptive-trigger resistance is not represented as a successful test.
- [ ] Inspect enumerated SDL audio endpoint names in the exported report. Do not assume generic output is the controller speaker or capture input is its microphone.
- [ ] No controller speaker test or microphone capture is performed by this lab unless a separately verified controller-specific route is implemented later.

## Export and regression

- [ ] Export the report and verify it is valid JSON at the platform path documented in [DualSense Lab](DUALSENSE_LAB.md).
- [ ] Confirm API availability and hardware verification are distinct in the report.
- [ ] Confirm rumble stops on lab exit, disconnect and application shutdown.
- [ ] Return to the main menu and start the game; verify gameplay input still works.
- [ ] On PS5, separately record application launch/exit results. Static package validation is not a hardware test.

## Result vocabulary

- **SUPPORTED:** live input/sensor data was observed, or the operator confirmed an output effect.
- **UNSUPPORTED:** the connected backend explicitly reports no implementation/capability.
- **NOT TESTED:** no functional hardware result is available yet.
- **FAILED:** a supported API operation returned an error, or the operator confirmed the effect did not work.

## Recorded PS5 evidence (2026-11-20)

The operator supplied a screenshot of the running lab and an exported PS5 report (SDL 2.30.12). The report records observed buttons, sticks and analog triggers; all four stick axes reached -32767 and +32767, and both triggers reached 0 and 32767. This does not verify each individual button mapping.

The operator separately confirmed that ordinary vibration and LED output worked. The exported snapshot still marks these outputs NOT TESTED because it contains no in-app confirmation. To record them in a new report, run Cross, confirm the felt vibration with L1, then run Square and confirm the visible LED output with L1 before exporting with Triangle. Use R1 instead if an expected effect is absent.

Touch contacts, sensors, trigger rumble, adaptive resistance and controller-specific audio remain unsupported through the pinned PS5 backend. Reconnect, effect duration/stop behavior, the complete LED color sequence/reset and gameplay regression on this console build have not been confirmed.
