#include "echo/framework/data.hpp"

#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check(echo::framework::Status status, std::string_view message) {
    check(static_cast<bool>(status), message);
}

void testJsonAndContent() {
    using namespace echo::framework;
    constexpr char manifest[] =
        R"({"resources":[{"id":"echo:ability/echo_step","type":"ability"},)"
        R"({"id":"echo:sprite/player","type":"sprite","dependsOn":["echo:ability/echo_step"]}]})";
    JsonDocument document{};
    JsonError error{};
    check(JsonDocument::parse(manifest, sizeof(manifest) - 1, document, error),
          "bounded in-memory JSON parses");
    check(document.root().isObject(), "JSON wrapper exposes the root without yyjson types");
    ContentRegistry registry{};
    check(loadResourceManifest(document.root(), registry), "resource manifest loads and validates");
    ResourceInfo resource{};
    check(registry.find("echo:sprite/player", resource) &&
              resource.type == ResourceType::Sprite,
          "content lookup returns stable ID and resource type");
    check(registry.find("echo:item/missing", resource).code == ErrorCode::NotFound,
          "missing content references are explicit");

    constexpr char missingDependency[] =
        R"({"resources":[{"id":"echo:sprite/player","type":"sprite",)"
        R"("dependsOn":["echo:item/missing"]}]})";
    JsonDocument missingDocument{};
    check(JsonDocument::parse(missingDependency, sizeof(missingDependency) - 1,
                              missingDocument, error),
          "missing-reference fixture parses");
    check(loadResourceManifest(missingDocument.root(), registry).code ==
              ErrorCode::MissingDependency,
          "required content dependencies are validated");

    constexpr char optionalDependency[] =
        R"({"resources":[{"id":"echo:sprite/optional","type":"sprite",)"
        R"("optionalDependencies":["echo:item/not_installed"]}]})";
    JsonDocument optionalDocument{};
    check(JsonDocument::parse(optionalDependency, sizeof(optionalDependency) - 1,
                              optionalDocument, error) &&
              loadResourceManifest(optionalDocument.root(), registry),
          "missing optional content references do not block the manifest");

    constexpr char invalidJson[] = R"({"key":)";
    JsonDocument malformed{};
    const Status malformedStatus =
        JsonDocument::parse(invalidJson, sizeof(invalidJson) - 1, malformed, error);
    check(malformedStatus.code == ErrorCode::ParseError && error.offset > 0,
          "malformed JSON reports a parse offset");
    const char invalidUtf8[] = {'{', '"', 'x', '"', ':', '"', static_cast<char>(0xff), '"', '}'};
    JsonDocument invalidUtf8Document{};
    check(JsonDocument::parse(invalidUtf8, sizeof(invalidUtf8), invalidUtf8Document, error).code ==
              ErrorCode::ParseError,
          "malformed UTF-8 is rejected");
    static std::array<char, kMaxJsonBytes + 1> oversized{};
    check(JsonDocument::parse(oversized.data(), oversized.size(), malformed, error).code ==
              ErrorCode::CapacityExceeded,
          "JSON input limit is enforced before parsing");
}

struct FakeStorage {
    std::array<char, echo::framework::kMaxSaveBytes> current{};
    std::array<char, echo::framework::kMaxSaveBytes> temporary{};
    std::array<char, 128> slots{};
    std::size_t currentSize{};
    std::size_t temporarySize{};
    std::size_t slotListSize{};
    bool failWrite{};
    bool failReplace{};
    bool failRead{};
};

bool readFake(void* raw, std::string_view slot, char* output,
              std::size_t capacity, std::size_t& size) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a" || storage.failRead || storage.currentSize > capacity) {
        return false;
    }
    std::memcpy(output, storage.current.data(), storage.currentSize);
    size = storage.currentSize;
    return true;
}

bool writeTemporaryFake(void* raw, std::string_view slot,
                        const char* data, std::size_t size) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a" || storage.failWrite || size > storage.temporary.size()) {
        return false;
    }
    std::memcpy(storage.temporary.data(), data, size);
    storage.temporarySize = size;
    return true;
}

bool replaceFake(void* raw, std::string_view slot) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a" || storage.failReplace) {
        return false;
    }
    std::memcpy(storage.current.data(), storage.temporary.data(), storage.temporarySize);
    storage.currentSize = storage.temporarySize;
    storage.temporarySize = 0;
    return true;
}

bool removeTemporaryFake(void* raw, std::string_view slot) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a") {
        return false;
    }
    storage.temporarySize = 0;
    return true;
}

bool enumerateFake(void* raw, char* output, std::size_t capacity,
                   std::size_t& written) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (storage.slotListSize > capacity) {
        return false;
    }
    std::memcpy(output, storage.slots.data(), storage.slotListSize);
    written = storage.slotListSize;
    return true;
}

bool migrateV1(std::uint32_t fromVersion, echo::framework::GameState& state,
               void*) noexcept {
    return fromVersion == 1 &&
           static_cast<bool>(state.set({echo::framework::StateScope::Global, {},
                                        "save.migrated"},
                                       echo::framework::StateValue::fromBoolean(true)));
}

void testSaveCodecAndStorage() {
    using namespace echo::framework;
    GameState state{};
    StateValue stringValue{};
    check(StateValue::fromString("line one\n\"line two\", \"integrity\":\"nested\"", stringValue),
          "bounded UTF-8 state string is created");
    check(state.set({StateScope::Global, {}, "story.generator_repaired"},
                    StateValue::fromBoolean(true)),
          "global state is stored before saving");
    check(state.set({StateScope::World, {}, "world.level"}, StateValue::fromInteger(7)),
          "world state is stored before saving");
    check(state.set({StateScope::Entity, {4, 9}, "player.health"},
                    StateValue::fromNumber(3.5)),
          "entity-scoped numeric state is stored before saving");
    check(state.set({StateScope::Global, {}, "player.name"}, stringValue),
          "string state is stored before saving");

    constexpr std::array<ModuleVersion, 2> modules{{
        {"echo.core", 1, true},
        {"echo.dialogue", 1, false},
    }};
    std::array<char, kMaxSaveBytes> save{};
    std::size_t saveSize{};
    check(SaveCodec::encode("echo.project_echo", 1, modules, state,
                            save.data(), save.size(), saveSize),
          "versioned save serializes to bounded JSON");
    check(saveSize != 0 && saveSize < save.size(), "save writer reports its output size");

    GameState restored{};
    SaveHeader header{};
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 1,
                            modules, restored, header),
          "versioned save restores into a separate state instance");
    StateValue restoredValue{};
    check(header.formatVersion == 1 && header.moduleCount == 2 &&
              restored.get({StateScope::Global, {}, "story.generator_repaired"}, restoredValue) &&
              restoredValue.boolean,
          "save header and global state round-trip");
    check(restored.get({StateScope::World, {}, "world.level"}, restoredValue) &&
              restoredValue.integer == 7,
          "world state round-trips");
    check(restored.get({StateScope::Entity, {4, 9}, "player.health"}, restoredValue) &&
              restoredValue.number == 3.5,
          "entity-scoped state round-trips");
    check(restored.get({StateScope::Global, {}, "player.name"}, restoredValue) &&
              restoredValue.stringValue() == "line one\n\"line two\", \"integrity\":\"nested\"",
          "escaped UTF-8 string state round-trips");

    check(SaveCodec::decode(save.data(), saveSize, "echo.other_game", 1,
                            modules, restored, header).code == ErrorCode::InvalidValue,
          "saves cannot be loaded by another game identifier");
    constexpr std::array<ModuleVersion, 1> incompatible{{{"echo.core", 2, true}}};
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 1,
                            incompatible, restored, header).code == ErrorCode::UnsupportedVersion,
          "module version mismatches are rejected");
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 2,
                            modules, restored, header).code == ErrorCode::UnsupportedVersion,
          "format mismatch without migration is rejected");
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 2,
                            modules, restored, header, &migrateV1),
          "registered save migration permits an older compatible schema");
    check(restored.contains({StateScope::Global, {}, "save.migrated"}),
          "migration runs on validated candidate state");
    std::array<char, kMaxSaveBytes> damaged{};
    std::memcpy(damaged.data(), save.data(), saveSize);
    damaged[10] = damaged[10] == '1' ? '2' : '1';
    check(SaveCodec::decode(damaged.data(), saveSize, "echo.project_echo", 1,
                            modules, restored, header).code == ErrorCode::CorruptData,
          "save integrity hash detects modified content");
    check(SaveCodec::decode("{", 1, "echo.project_echo", 1,
                            modules, restored, header).code == ErrorCode::CorruptData,
          "save without an integrity record is rejected");
    constexpr char missingState[] =
        R"({"format":1,"game":"echo.project_echo","modules":[],"integrity":"e3f722e4d9d0543b"})";
    const Status missingStateStatus = SaveCodec::decode(
        missingState, sizeof(missingState) - 1, "echo.project_echo", 1,
        modules, restored, header);
    check(missingStateStatus.code == ErrorCode::CorruptData &&
              missingStateStatus.detail == "Save is missing required fields or exceeds schema limits." &&
              restored.contains({StateScope::Global, {}, "save.migrated"}),
          "checksummed save missing required state data fails without changing destination state");
    check(SaveCodec::decode(save.data(), kMaxSaveBytes + 1, "echo.project_echo", 1,
                            modules, restored, header).code == ErrorCode::InvalidArgument,
          "excessively large save is rejected before parsing");

    FakeStorage fake{};
    std::memcpy(fake.current.data(), "old-valid-save", 14);
    fake.currentSize = 14;
    fake.failReplace = true;
    SaveStorage storage{&fake, &readFake, &writeTemporaryFake, &replaceFake,
                        &removeTemporaryFake, &enumerateFake};
    check(SaveService::save(storage, "slot_a", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()).code ==
              ErrorCode::StorageFailure,
          "failed replacement is reported");
    check(fake.currentSize == 14 &&
              std::string_view(fake.current.data(), fake.currentSize) == "old-valid-save" &&
              fake.temporarySize == 0,
          "failed replacement preserves the previous save and removes only the temporary");
    fake.failReplace = false;
    fake.failWrite = true;
    check(SaveService::save(storage, "slot_a", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()).code ==
              ErrorCode::StorageFailure && fake.currentSize == 14,
          "failed temporary write preserves the previous save");
    fake.failWrite = false;
    check(SaveService::save(storage, "slot_a", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()),
          "storage abstraction writes and replaces a slot");
    GameState loaded{};
    check(SaveService::load(storage, "slot_a", "echo.project_echo", 1,
                            modules, loaded, header, save.data(), save.size()),
          "storage abstraction reads and validates a slot");
    fake.failRead = true;
    check(SaveService::load(storage, "slot_a", "echo.project_echo", 1,
                            modules, loaded, header, save.data(), save.size()).code ==
              ErrorCode::StorageFailure,
          "failed storage read is reported");
    fake.failRead = false;
    check(SaveService::save(storage, "../unrelated", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()).code ==
              ErrorCode::InvalidArgument,
          "unsafe slot paths are rejected");
    check(SaveService::validSlot("quick-save_1") && !SaveService::validSlot(".."),
          "slot IDs accept only a bounded safe character set");

    std::memcpy(fake.slots.data(), "slot_a\0slot-b\0", 14);
    fake.slotListSize = 14;
    std::array<char, 128> slotOutput{};
    std::size_t slotOutputSize{};
    check(SaveService::enumerate(storage, slotOutput.data(), slotOutput.size(), slotOutputSize) &&
              slotOutputSize == fake.slotListSize,
          "slot enumeration validates and returns bounded NUL-separated IDs");
}

} // namespace

int main() {
    testJsonAndContent();
    testSaveCodecAndStorage();
    if (failures != 0) {
        std::cerr << failures << " framework data test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework data tests passed\n";
    return EXIT_SUCCESS;
}
