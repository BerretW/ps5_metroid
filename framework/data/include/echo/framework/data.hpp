#pragma once

#include "echo/framework/core.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxJsonBytes = 1024 * 1024;
inline constexpr std::size_t kMaxAssetJsonBytes = 16 * 1024 * 1024;
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
    [[nodiscard]] bool memberAt(std::size_t index, std::string_view& key,
                                JsonValue& value) const noexcept;
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
    [[nodiscard]] static Status parseAsset(const char* data, std::size_t size,
                                           JsonDocument& output, JsonError& error) noexcept;
    [[nodiscard]] JsonValue root() const noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] static Status parseBounded(const char* data, std::size_t size,
                                             std::size_t limit, JsonDocument& output,
                                             JsonError& error) noexcept;
    void* document_{};
};

class JsonConditionTree {
public:
    static constexpr std::size_t kMaxNodes = 128;
    static constexpr std::size_t kMaxChildren = 32;

    JsonConditionTree() noexcept = default;
    JsonConditionTree(const JsonConditionTree&) = delete;
    JsonConditionTree& operator=(const JsonConditionTree&) = delete;
    JsonConditionTree(JsonConditionTree&&) = delete;
    JsonConditionTree& operator=(JsonConditionTree&&) = delete;

    [[nodiscard]] Status parse(JsonValue document) noexcept;
    [[nodiscard]] const Condition* root() const noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] Status parseNode(JsonValue document, std::size_t nodeIndex,
                                   std::size_t depth) noexcept;

    std::array<Condition, kMaxNodes> nodes_{};
    std::array<std::array<char, 64>, kMaxNodes> keys_{};
    std::size_t nodeCount_{};
    bool valid_{};
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
    std::string_view source{};
};

struct ResourceInfo {
    std::string_view id;
    ResourceType type{ResourceType::Generic};
    bool optional{};
    std::string_view source{};
};

class ContentRegistry {
public:
    static constexpr std::size_t kMaxResources = 64;
    static constexpr std::size_t kMaxDependencies = 8;

    [[nodiscard]] Status add(const ResourceSpec& resource) noexcept;
    [[nodiscard]] Status find(std::string_view id, ResourceInfo& resource) const noexcept;
    [[nodiscard]] bool at(std::size_t index, ResourceInfo& resource) const noexcept;
    [[nodiscard]] Status validate() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    struct Dependency {
        std::array<char, 128> id{};
        bool required{};
    };

    struct Resource {
        std::array<char, 128> id{};
        std::array<char, 128> source{};
        ResourceType type{ResourceType::Generic};
        std::array<Dependency, kMaxDependencies> dependencies{};
        std::size_t dependencyCount{};
        bool optional{};
        bool active{};
    };

    std::array<Resource, kMaxResources> resources_{};
    std::size_t count_{};
};

[[nodiscard]] bool isValidContentId(std::string_view id) noexcept;
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

#if defined(_WIN32)
class WindowsSaveStorage {
public:
    WindowsSaveStorage() noexcept = default;
    ~WindowsSaveStorage();
    WindowsSaveStorage(const WindowsSaveStorage&) = delete;
    WindowsSaveStorage& operator=(const WindowsSaveStorage&) = delete;
    WindowsSaveStorage(WindowsSaveStorage&&) = delete;
    WindowsSaveStorage& operator=(WindowsSaveStorage&&) = delete;

    [[nodiscard]] Status initialize(std::wstring_view subdirectory = L"Saves") noexcept;
    [[nodiscard]] SaveStorage callbacks() noexcept;

private:
    static bool readCallback(void* context, std::string_view slot, char* output,
                             std::size_t capacity, std::size_t& size) noexcept;
    static bool writeTemporaryCallback(void* context, std::string_view slot,
                                       const char* data, std::size_t size) noexcept;
    static bool replaceCallback(void* context, std::string_view slot) noexcept;
    static bool removeTemporaryCallback(void* context, std::string_view slot) noexcept;
    static bool enumerateCallback(void* context, char* output,
                                  std::size_t capacity, std::size_t& written) noexcept;

    bool read(std::string_view slot, char* output, std::size_t capacity,
              std::size_t& size) noexcept;
    bool writeTemporary(std::string_view slot, const char* data,
                        std::size_t size) noexcept;
    bool replace(std::string_view slot) noexcept;
    bool removeTemporary(std::string_view slot) noexcept;
    bool enumerate(char* output, std::size_t capacity,
                   std::size_t& written) noexcept;
    void reset() noexcept;

    std::array<wchar_t, 1024> rootPath_{};
    std::array<char, 33> temporarySlot_{};
    void* rootHandle_{};
    void* temporaryHandle_{};
};
#endif

#if defined(__PROSPERO__)
class Ps5SaveStorage {
public:
    Ps5SaveStorage() noexcept = default;
    ~Ps5SaveStorage();
    Ps5SaveStorage(const Ps5SaveStorage&) = delete;
    Ps5SaveStorage& operator=(const Ps5SaveStorage&) = delete;
    Ps5SaveStorage(Ps5SaveStorage&&) = delete;
    Ps5SaveStorage& operator=(Ps5SaveStorage&&) = delete;

    [[nodiscard]] Status initialize(std::string_view subdirectory = "saves") noexcept;
    [[nodiscard]] SaveStorage callbacks() noexcept;
    // Describes how the directory checks were performed during initialize().
    [[nodiscard]] std::string_view diagnostics() const noexcept {
        return {diagnostics_.data()};
    }

private:
    static bool readCallback(void* context, std::string_view slot, char* output,
                             std::size_t capacity, std::size_t& size) noexcept;
    static bool writeTemporaryCallback(void* context, std::string_view slot,
                                       const char* data, std::size_t size) noexcept;
    static bool replaceCallback(void* context, std::string_view slot) noexcept;
    static bool removeTemporaryCallback(void* context, std::string_view slot) noexcept;
    static bool enumerateCallback(void* context, char* output,
                                  std::size_t capacity, std::size_t& written) noexcept;

    bool read(std::string_view slot, char* output, std::size_t capacity,
              std::size_t& size) noexcept;
    bool writeTemporary(std::string_view slot, const char* data,
                        std::size_t size) noexcept;
    bool replace(std::string_view slot) noexcept;
    bool removeTemporary(std::string_view slot) noexcept;
    bool enumerate(char* output, std::size_t capacity,
                   std::size_t& written) noexcept;
    void reset() noexcept;
    bool fail(const char* step) noexcept;
    bool parseDirectoryChunk(const char* buffer, std::size_t bytes, char* output,
                             std::size_t capacity, std::size_t& resultSize) noexcept;

    std::array<char, 96> rootPath_{};
    std::array<char, 512> diagnostics_{};
    std::array<char, 128> temporaryPath_{};
    std::array<char, 33> temporarySlot_{};
    std::uint32_t temporarySequence_{};
};
#endif

} // namespace echo::framework
