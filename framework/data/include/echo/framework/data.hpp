#pragma once

#include "echo/framework/core.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxJsonBytes = 1024 * 1024;
inline constexpr std::size_t kMaxSaveBytes = 64 * 1024;

struct JsonError {
    std::size_t offset{};
    std::string_view message{};
};

class JsonValue {
public:
    JsonValue() noexcept = default;

    [[nodiscard]] bool isObject() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isBoolean() const noexcept;
    [[nodiscard]] bool isInteger() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] JsonValue find(std::string_view key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] JsonValue at(std::size_t index) const noexcept;
    [[nodiscard]] bool string(std::string_view& value) const noexcept;
    [[nodiscard]] bool boolean(bool& value) const noexcept;
    [[nodiscard]] bool integer(std::int64_t& value) const noexcept;
    [[nodiscard]] bool number(double& value) const noexcept;
    [[nodiscard]] bool valid() const noexcept { return value_ != nullptr; }

private:
    explicit JsonValue(const void* value) noexcept : value_(value) {}
    const void* value_{};
    friend class JsonDocument;
};

class JsonDocument {
public:
    JsonDocument() noexcept = default;
    ~JsonDocument();
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;

    [[nodiscard]] static Status parse(const char* data, std::size_t size,
                                      JsonDocument& output, JsonError& error) noexcept;
    [[nodiscard]] JsonValue root() const noexcept;
    void reset() noexcept;

private:
    void* document_{};
};

enum class ResourceType : std::uint8_t {
    Generic,
    Item,
    Ability,
    Dialogue,
    Cutscene,
    Sprite,
    Sound
};

struct ContentDependency {
    std::string_view id;
    bool required{true};
};

struct ResourceSpec {
    std::string_view id;
    ResourceType type{ResourceType::Generic};
    bool optional{};
    std::span<const ContentDependency> dependencies;
};

struct ResourceInfo {
    std::string_view id;
    ResourceType type{ResourceType::Generic};
    bool optional{};
};

class ContentRegistry {
public:
    static constexpr std::size_t kMaxResources = 64;
    static constexpr std::size_t kMaxDependencies = 8;

    [[nodiscard]] Status add(const ResourceSpec& resource) noexcept;
    [[nodiscard]] Status find(std::string_view id, ResourceInfo& resource) const noexcept;
    [[nodiscard]] Status validate() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    struct Dependency {
        std::array<char, 128> id{};
        bool required{};
    };

    struct Resource {
        std::array<char, 128> id{};
        ResourceType type{ResourceType::Generic};
        std::array<Dependency, kMaxDependencies> dependencies{};
        std::size_t dependencyCount{};
        bool optional{};
        bool active{};
    };

    std::array<Resource, kMaxResources> resources_{};
    std::size_t count_{};
};

[[nodiscard]] Status loadResourceManifest(JsonValue document,
                                          ContentRegistry& output) noexcept;

struct ModuleVersion {
    std::string_view id;
    std::uint32_t version{};
    bool required{true};
};

struct SaveHeader {
    std::array<char, 64> gameId{};
    std::uint32_t formatVersion{};
    std::array<std::array<char, 64>, 16> moduleIds{};
    std::array<std::uint32_t, 16> moduleVersions{};
    std::size_t moduleCount{};
};

using SaveMigration = bool (*)(std::uint32_t fromVersion, GameState& candidate,
                               void* context) noexcept;

class SaveCodec {
public:
    [[nodiscard]] static Status encode(std::string_view gameId,
                                       std::uint32_t formatVersion,
                                       std::span<const ModuleVersion> modules,
                                       const GameState& state,
                                       char* output, std::size_t capacity,
                                       std::size_t& written) noexcept;
    [[nodiscard]] static Status decode(const char* data, std::size_t size,
                                       std::string_view expectedGameId,
                                       std::uint32_t currentFormatVersion,
                                       std::span<const ModuleVersion> expectedModules,
                                       GameState& output, SaveHeader& header,
                                       SaveMigration migration = nullptr,
                                       void* migrationContext = nullptr) noexcept;
};

struct SaveStorage {
    void* context{};
    bool (*read)(void*, std::string_view, char*, std::size_t, std::size_t&) noexcept{};
    bool (*writeTemporary)(void*, std::string_view, const char*, std::size_t) noexcept{};
    bool (*replace)(void*, std::string_view) noexcept{};
    bool (*removeTemporary)(void*, std::string_view) noexcept{};
    bool (*enumerate)(void*, char*, std::size_t, std::size_t&) noexcept{};
};

class SaveService {
public:
    [[nodiscard]] static Status save(const SaveStorage& storage, std::string_view slot,
                                     std::string_view gameId, std::uint32_t formatVersion,
                                     std::span<const ModuleVersion> modules,
                                     const GameState& state, char* scratch,
                                     std::size_t scratchCapacity) noexcept;
    [[nodiscard]] static Status load(const SaveStorage& storage, std::string_view slot,
                                     std::string_view expectedGameId,
                                     std::uint32_t currentFormatVersion,
                                     std::span<const ModuleVersion> expectedModules,
                                     GameState& output, SaveHeader& header,
                                     char* scratch, std::size_t scratchCapacity,
                                     SaveMigration migration = nullptr,
                                     void* migrationContext = nullptr) noexcept;
    [[nodiscard]] static Status enumerate(const SaveStorage& storage, char* output,
                                         std::size_t capacity, std::size_t& written) noexcept;
    [[nodiscard]] static bool validSlot(std::string_view slot) noexcept;
};

} // namespace echo::framework
