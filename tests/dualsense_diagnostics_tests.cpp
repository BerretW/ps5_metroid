#include "input/dualsense_diagnostics.hpp"

#include <SDL.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

bool collectReport(void* context, const char* data, std::size_t size) {
    static_cast<std::string*>(context)->append(data, size);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using namespace echo::input;
    const AxisPair minimum = normalizeStick(-32768, 0);
    const AxisPair maximum = normalizeStick(32767, 0);
    check(near(minimum.x, -1.0F) && near(minimum.y, 0.0F),
          "stick normalization handles the negative endpoint");
    check(near(maximum.x, 1.0F), "stick normalization handles the positive endpoint");

    check(near(applyRadialDeadZone({0.1F, 0.0F}, 0.15F).x, 0.0F),
          "radial dead zone removes center drift");
    const AxisPair outside = applyRadialDeadZone({0.5F, 0.0F}, 0.15F);
    check(near(outside.x, (0.5F - 0.15F) / 0.85F) && near(outside.y, 0.0F),
          "radial dead zone rescales input beyond its threshold");
    check(near(applyRadialDeadZone({1.0F, 0.0F}, 0.15F).x, 1.0F),
          "radial dead zone preserves full travel");
    check(near(normalizeTrigger(-32768), 0.0F) && near(normalizeTrigger(0), 0.0F) &&
              near(normalizeTrigger(32767), 1.0F),
          "trigger normalization maps released and fully pressed positions");
    check(near(normalizeTrigger(-12000), 0.0F),
          "trigger normalization clamps values below the released position");
    check(near(normalizeTouchCoordinate(-0.2F), 0.0F) &&
              near(normalizeTouchCoordinate(0.5F), 0.5F) &&
              near(normalizeTouchCoordinate(1.2F), 1.0F) &&
              near(normalizeTouchCoordinate(std::numeric_limits<float>::quiet_NaN()), 0.0F),
          "touch coordinates remain within the rendered touchpad");
    static_assert(kMaxTestEffectDurationMs <= 500,
                  "user-triggered diagnostic effects must remain short");

    ControllerSnapshot snapshot;
    snapshot.connected = true;
    setText(snapshot.name, "DualSense \"Test\"");
    snapshot.instanceId = 42;
    snapshot.rumbleAvailable = true;
    snapshot.triggerRumbleAvailable = false;
    snapshot.ledAvailable = true;
    setText(snapshot.lastAction, "RUMBLE TEST");
    snapshot.lastResult = -1;
    setText(snapshot.lastError, "device\\error\n");
    snapshot.hasResult = true;
    snapshot.rumbleCallFailed = true;
    snapshot.triggerRumbleAvailable = false;
    std::string report;
    check(writeReport(snapshot, "Windows", collectReport, &report),
          "report writer completes through the supplied sink");
    check(report.find("\"status\":\"FAILED\"") != std::string::npos,
          "failed attempted rumble is reported as FAILED");
    check(report.find("\"trigger_rumble\":{\"status\":\"UNSUPPORTED\"") != std::string::npos &&
              report.find("\"adaptive_triggers\":{\"status\":\"NOT TESTED\"") != std::string::npos,
          "trigger rumble and adaptive resistance have separate statuses");
    check(report.find("DualSense \\\"Test\\\"") != std::string::npos &&
              report.find("device\\\\error\\n") != std::string::npos,
          "report serialization escapes strings");
    check(report.find("\"hardware_verified\":false") != std::string::npos,
          "API availability and hardware verification are separate");

    if (failures != 0) {
        return EXIT_FAILURE;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--emit-report") {
        std::cout << report;
        return EXIT_SUCCESS;
    }
    std::cout << "DualSense diagnostics math and report tests passed\n";
    return EXIT_SUCCESS;
}
