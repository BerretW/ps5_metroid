#include "echo/framework/inventory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 2> kModuleServices{
    "echo.service.inventory", "echo.service.items"};
constexpr std::array<std::string_view, 2> kItemDocumentFields{
    "schemaVersion", "items"};
constexpr std::array<std::string_view, 4> kItemFields{
    "id", "key", "displayName", "maxStack"};
const ModuleInfo kInventoryModuleInfo{
    "echo.rpg.inventory", "Inventory and item catalog", 1,
    {}, {}, kModuleServices, {}};
const std::array<ManifestModuleDescriptor, 1> kManifestModules{{
    {"echo.rpg.inventory", "Inventory and item catalog", 1, {}, {}, kModuleServices, {}},
}};
const std::array<ManifestSchemaDescriptor, 1> kManifestSchemas{{
    {"echo.rpg.item_catalog", 1},
}};

[[nodiscard]] bool hasOnlyFields(
    JsonValue object, std::span<const std::string_view> allowed) noexcept {
    if (!object.isObject() || object.size() != allowed.size()) return false;
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousName;
            JsonValue previousValue{};
            if (!object.memberAt(previous, previousName, previousValue) ||
                previousName == name) {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool copyBounded(std::string_view value,
                               std::array<char, Capacity>& output) noexcept {
    if (value.empty() || value.size() >= output.size() ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    std::memcpy(output.data(), value.data(), value.size());
    output[value.size()] = '\0';
    return true;
}

} // namespace

Status ItemCatalog::load(JsonValue document) noexcept {
    ItemCatalog candidate{};
    if (!hasOnlyFields(document, kItemDocumentFields)) {
        return {ErrorCode::InvalidValue, "item_catalog",
                "Item catalog requires exactly schemaVersion and items fields."};
    }
    std::int64_t version{};
    const JsonValue entries = document.find("items");
    if (!document.find("schemaVersion").integer(version) || version != 1 ||
        !entries.isArray() || entries.size() == 0 ||
        entries.size() > kMaxItems) {
        return {ErrorCode::UnsupportedVersion, "item_catalog",
                "Item catalog schema version or bounded item list is invalid."};
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const JsonValue entry = entries.at(index);
        std::string_view id;
        std::string_view name;
        std::int64_t key{};
        std::int64_t maxStack{};
        if (!hasOnlyFields(entry, kItemFields) ||
            !entry.find("id").string(id) || !isValidContentId(id) ||
            !entry.find("key").integer(key) || key <= 0 ||
            key > std::numeric_limits<std::uint32_t>::max() ||
            !entry.find("displayName").string(name) ||
            !entry.find("maxStack").integer(maxStack) ||
            maxStack <= 0 || maxStack > std::numeric_limits<std::uint16_t>::max()) {
            return {ErrorCode::InvalidValue, "item_catalog.items",
                    "Each item requires a stable ID, unique positive key, display name, and bounded stack limit."};
        }
        if (candidate.find(id) != nullptr ||
            candidate.find(static_cast<std::uint32_t>(key)) != nullptr) {
            return {ErrorCode::DuplicateId, id,
                    "Item IDs and numeric keys must be unique."};
        }
        ItemDefinition& item = candidate.items_[candidate.count_];
        if (!copyBounded(id, item.id) || !copyBounded(name, item.displayName)) {
            return {ErrorCode::CapacityExceeded, id,
                    "Item ID or display name exceeds its fixed storage limit."};
        }
        item.key = static_cast<std::uint32_t>(key);
        item.maxStack = static_cast<std::uint16_t>(maxStack);
        ++candidate.count_;
    }
    *this = candidate;
    return Status::success();
}

const ItemDefinition* ItemCatalog::find(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (std::string_view(items_[index].id.data()) == id) return &items_[index];
    }
    return nullptr;
}

const ItemDefinition* ItemCatalog::find(std::uint32_t key) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (items_[index].key == key) return &items_[index];
    }
    return nullptr;
}

void ItemCatalog::clear() noexcept {
    items_.fill({});
    count_ = 0;
}

Status InventoryModule::load(JsonValue itemDocument) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, "echo.rpg.inventory",
                "Item definitions cannot change while the module is initialized."};
    }
    const Status status = catalog_.load(itemDocument);
    if (!status) return status;
    loaded_ = true;
    return Status::success();
}

const ModuleInfo& InventoryModule::info() const noexcept {
    return kInventoryModuleInfo;
}

bool InventoryModule::initialize(ModuleContext&) noexcept {
    initialized_ = loaded_ && catalog_.size() != 0;
    return initialized_;
}

void InventoryModule::shutdown() noexcept {
    initialized_ = false;
}

CapabilityManifestFragment inventoryCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, kManifestSchemas, {}};
}

} // namespace echo::framework
