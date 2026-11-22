#include "echo/framework/vfx.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace echo::vfx {
namespace {

using framework::ErrorCode;
using framework::JsonValue;
using framework::Status;

constexpr std::array<std::string_view, 16> kEmitterFields{
    "schemaVersion", "id", "maximumParticles", "minimumLifetime",
    "maximumLifetime", "emissionRate", "position", "initialVelocity",
    "velocitySpread", "gravity", "startColor", "endColor", "startScale",
    "endScale", "randomSeed", "continuous"};
constexpr std::array<std::string_view, 3> kVectorFields{"x", "y", "z"};

template <std::size_t Count>
[[nodiscard]] Status checkFields(
    JsonValue object, const std::array<std::string_view, Count>& allowed,
    std::string_view subject) noexcept {
    if (!object.isObject() || object.size() > allowed.size()) {
        return {ErrorCode::InvalidValue, subject,
                "Object shape or number of fields is invalid."};
    }
    std::array<std::string_view, Count> seen{};
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return {ErrorCode::InvalidValue, subject,
                    "Emitter configuration contains an unsupported field."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (seen[previous] == name) {
                return {ErrorCode::DuplicateId, subject,
                        "Emitter configuration contains a duplicate field."};
            }
        }
        seen[index] = name;
    }
    return Status::success();
}

[[nodiscard]] bool parseNumber(JsonValue value, float& output) noexcept {
    double number{};
    if (!value.number(number) || !std::isfinite(number) ||
        std::abs(number) > 1000000.0) {
        return false;
    }
    output = static_cast<float>(number);
    return std::isfinite(output);
}

[[nodiscard]] bool parseVector(JsonValue value, rendering::Vec3& output) noexcept {
    if (!value.isObject() || value.size() != kVectorFields.size()) {
        return false;
    }
    for (std::size_t index = 0; index < kVectorFields.size(); ++index) {
        std::string_view name;
        JsonValue component{};
        if (!value.memberAt(index, name, component) ||
            std::find(kVectorFields.begin(), kVectorFields.end(), name) ==
                kVectorFields.end()) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousName;
            JsonValue ignored{};
            if (value.memberAt(previous, previousName, ignored) &&
                previousName == name) {
                return false;
            }
        }
    }
    return parseNumber(value.find("x"), output.x) &&
           parseNumber(value.find("y"), output.y) &&
           parseNumber(value.find("z"), output.z);
}

[[nodiscard]] bool parseColor(JsonValue value, rendering::Vec4& output) noexcept {
    if (!value.isArray() || value.size() != 4) return false;
    return parseNumber(value.at(0), output.x) &&
           parseNumber(value.at(1), output.y) &&
           parseNumber(value.at(2), output.z) &&
           parseNumber(value.at(3), output.w);
}

[[nodiscard]] bool parseUnsigned(JsonValue value, std::uint32_t& output) noexcept {
    std::int64_t number{};
    if (!value.integer(number) || number < 0 ||
        number > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    output = static_cast<std::uint32_t>(number);
    return true;
}

} // namespace

Status EmitterDocument::parse(const char* data, std::size_t size,
                              framework::JsonError& error) noexcept {
    error = {};
    reset();
    Status status = framework::JsonDocument::parse(data, size, document_, error);
    if (!status) return status;
    const JsonValue root = document_.root();
    status = checkFields(root, kEmitterFields, "emitter");
    if (!status) {
        reset();
        return status;
    }

    std::int64_t schemaVersion{};
    std::string_view id;
    std::int64_t maximumParticles{};
    if (!root.find("schemaVersion").integer(schemaVersion) || schemaVersion != 1) {
        reset();
        return {ErrorCode::UnsupportedVersion, "schemaVersion",
                "Emitter JSON schemaVersion must be 1."};
    }
    if (!root.find("id").string(id) || !framework::isValidContentId(id)) {
        reset();
        return {ErrorCode::InvalidValue, "id",
                "Emitter ID must use a stable content ID."};
    }
    if (!root.find("maximumParticles").integer(maximumParticles) ||
        maximumParticles < 1 ||
        maximumParticles > static_cast<std::int64_t>(kMaxParticlesPerEmitter)) {
        reset();
        return {ErrorCode::InvalidValue, "maximumParticles",
                "Emitter maximumParticles must be from 1 to 256."};
    }

    EmitterConfig config{};
    config.maximumParticles = static_cast<std::uint16_t>(maximumParticles);
    if (!parseNumber(root.find("minimumLifetime"), config.minimumLifetime) ||
        !parseNumber(root.find("maximumLifetime"), config.maximumLifetime) ||
        !parseNumber(root.find("emissionRate"), config.emissionRate) ||
        !parseVector(root.find("position"), config.position) ||
        !parseVector(root.find("initialVelocity"), config.initialVelocity) ||
        !parseVector(root.find("velocitySpread"), config.velocitySpread) ||
        !parseVector(root.find("gravity"), config.gravity) ||
        !parseColor(root.find("startColor"), config.startColor) ||
        !parseColor(root.find("endColor"), config.endColor) ||
        !parseNumber(root.find("startScale"), config.startScale) ||
        !parseNumber(root.find("endScale"), config.endScale) ||
        !parseUnsigned(root.find("randomSeed"), config.randomSeed) ||
        !root.find("continuous").boolean(config.continuous)) {
        reset();
        return {ErrorCode::InvalidValue, "emitter",
                "Emitter configuration has missing or invalid values."};
    }
    status = validateEmitterConfig(config);
    if (!status) {
        reset();
        return status;
    }
    definition_ = {id, config};
    loaded_ = true;
    return Status::success();
}

void EmitterDocument::reset() noexcept {
    document_.reset();
    definition_ = {};
    loaded_ = false;
}

} // namespace echo::vfx
