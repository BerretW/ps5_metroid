#include "capability_manifest_input.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check(echo::framework::Status status, const char* message) {
    check(static_cast<bool>(status), message);
}

echo::framework::Status validateText(
    const char* text, std::size_t size,
    const echo::framework::ComponentTypeRegistry& registry) {
    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    const echo::framework::Status parsed =
        echo::framework::JsonDocument::parse(text, size, document, error);
    if (!parsed) return parsed;
    echo::framework::JsonValue components{};
    return echo::framework::tools::validateCapabilityManifest(
        document.root(), registry, components);
}

} // namespace

int main() {
    using namespace echo::framework;
    ComponentTypeRegistry registry{};
    check(registerCoreComponentTypes(registry), "core component schema registers");

    std::array<char, 64 * 1024> output{};
    std::size_t written{};
    const CapabilityManifestFragment framework =
        frameworkCapabilityManifestFragment();
    check(exportCapabilityManifest(registry, {&framework, 1}, output.data(),
                                   output.size(), written),
          "capability manifest exports");
    JsonDocument document{};
    JsonError error{};
    check(JsonDocument::parse(output.data(), written, document, error),
          "capability manifest parses");
    JsonValue components{};
    check(tools::validateCapabilityManifest(document.root(), registry, components) &&
              components.size() == 1,
          "current manifest matches compiled component descriptors");

    constexpr char unsupportedVersion[] =
        R"({"manifestVersion":2,"framework":{"apiVersion":1,"minimumApiVersion":1},)"
        R"("compatibility":{"apiVersion":1,"supportedManifestVersions":[2]},)"
        R"("components":[]})";
    check(validateText(unsupportedVersion, sizeof(unsupportedVersion) - 1, registry).code ==
              ErrorCode::UnsupportedVersion,
          "unsupported manifest formats are rejected");

    constexpr char staleComponent[] =
        R"({"manifestVersion":1,"framework":{"apiVersion":1,"minimumApiVersion":1},)"
        R"("compatibility":{"apiVersion":1,"supportedManifestVersions":[1]},)"
        R"("components":[{"id":"echo:component/transform2d","version":2,)"
        R"("displayName":"Transform","properties":[]}]})";
    check(validateText(staleComponent, sizeof(staleComponent) - 1, registry).code ==
              ErrorCode::UnsupportedVersion,
          "stale component schema versions are rejected");

    constexpr char uncompiledComponent[] =
        R"({"manifestVersion":1,"framework":{"apiVersion":1,"minimumApiVersion":1},)"
        R"("compatibility":{"apiVersion":1,"supportedManifestVersions":[1]},)"
        R"("components":[{"id":"echo.project_echo:component/dialogue_interaction",)"
        R"("version":1,"displayName":"Dialogue interaction","properties":[]}]})";
    check(validateText(uncompiledComponent, sizeof(uncompiledComponent) - 1, registry).code ==
              ErrorCode::NotFound,
          "manifest cannot enable components absent from the compiled registry");

    constexpr char unsortedComponents[] =
        R"({"manifestVersion":1,"framework":{"apiVersion":1,"minimumApiVersion":1},)"
        R"("compatibility":{"apiVersion":1,"supportedManifestVersions":[1]},)"
        R"("components":[{"id":"echo:component/transform2d","version":1,)"
        R"("displayName":"Transform","properties":[]},)"
        R"({"id":"echo:component/aaa","version":1,"displayName":"A","properties":[]}]})";
    const Status unsortedStatus =
        validateText(unsortedComponents, sizeof(unsortedComponents) - 1, registry);
    check(unsortedStatus.code == ErrorCode::InvalidValue,
          "noncanonical component ordering is rejected");

    if (failures != 0) {
        std::cerr << failures << " capability manifest input test(s) failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
