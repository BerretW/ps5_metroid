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
constexpr std::array<std::string_view, 7> kItemFields{
    "id", "key", "displayName", "maxStack", "description", "use", "equipment"};
constexpr std::array<std::string_view, 2> kEquipmentFields{"id", "slot"};
constexpr std::array<std::string_view, 3> kHealFields{
    "effect", "amount", "attribute"};
constexpr std::array<std::string_view, 2> kShieldFields{
    "effect", "durationSteps"};
constexpr std::array<std::string_view, 3> kTimedBoostFields{
    "effect", "amount", "durationSteps"};
constexpr std::array<std::string_view, 3> kAreaDamageFields{
    "effect", "amount", "radius"};
const ModuleInfo kInventoryModuleInfo{
    "echo.rpg.inventory", "Inventory and item catalog", 1,
    {}, {}, kModuleServices, {}};
const std::array<ManifestModuleDescriptor, 1> kManifestModules{{
    {"echo.rpg.inventory", "Inventory and item catalog", 1, {}, {}, kModuleServices, {}},
}};
const std::array<ManifestSchemaDescriptor, 1> kManifestSchemas{{
    {"echo.rpg.item_catalog", 3},
}};

[[nodiscard]] bool hasOnlyFields(
    JsonValue object, std::span<const std::string_view> allowed,
    std::size_t minimumFields) noexcept {
    if (!object.isObject() || object.size() < minimumFields ||
        object.size() > allowed.size()) return false;
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
    if (!hasOnlyFields(document, kItemDocumentFields, kItemDocumentFields.size())) {
        return {ErrorCode::InvalidValue, "item_catalog",
                "Item catalog requires exactly schemaVersion and items fields."};
    }
    std::int64_t version{};
    const JsonValue entries = document.find("items");
    if (!document.find("schemaVersion").integer(version) ||
        (version < 1 || version > 3) ||
        !entries.isArray() || entries.size() == 0 ||
        entries.size() > kMaxItems) {
        return {ErrorCode::UnsupportedVersion, "item_catalog",
                "Item catalog schema version or bounded item list is invalid."};
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const JsonValue entry = entries.at(index);
        std::string_view id;
        std::string_view name;
        std::string_view equipmentId;
        std::string_view equipmentSlot;
        std::int64_t key{};
        std::int64_t maxStack{};
        if (!hasOnlyFields(entry, kItemFields, 4) ||
            (version == 1 && entry.size() != 4) ||
            (version == 2 && entry.find("equipment").valid()) ||
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
        std::string_view description;
        const JsonValue jsonUse = entry.find("use");
        if (!copyBounded(id, item.id) || !copyBounded(name, item.displayName)) {
            return {ErrorCode::CapacityExceeded, id,
                    "Item ID or display name exceeds its fixed storage limit."};
        }
        if (entry.find("description").valid()) {
            if (!entry.find("description").string(description)) {
                return {ErrorCode::InvalidValue, id,
                        "Item description must be a string."};
            }
            if (!copyBounded(description, item.description)) {
                return {ErrorCode::CapacityExceeded, id,
                        "Item description exceeds its fixed storage limit."};
            }
        }
        const JsonValue jsonEquipment = entry.find("equipment");
        if (jsonEquipment.valid()) {
            if (version < 3 ||
                !hasOnlyFields(jsonEquipment, kEquipmentFields, kEquipmentFields.size()) ||
                !jsonEquipment.find("id").string(equipmentId) ||
                !jsonEquipment.find("slot").string(equipmentSlot) ||
                !isValidContentId(equipmentId) || !isValidContentId(equipmentSlot) ||
                !copyBounded(equipmentId, item.equipmentId) ||
                !copyBounded(equipmentSlot, item.equipmentSlot)) {
                return {ErrorCode::InvalidValue, id,
                        "Item equipment bindings require bounded stable equipment and slot IDs."};
            }
        }
        item.key = static_cast<std::uint32_t>(key);
        item.maxStack = static_cast<std::uint16_t>(maxStack);
        if (jsonUse.valid()) {
            std::string_view effect;
            std::string_view attribute;
            std::int64_t amount{};
            std::int64_t durationSteps{};
            std::int64_t radius{};
            if (!jsonUse.find("effect").string(effect) ||
                !jsonUse.isObject()) {
                return {ErrorCode::InvalidValue, id,
                        "Item use effect has an invalid shape."};
            }
            if (effect == "heal") {
                if (!hasOnlyFields(jsonUse, kHealFields, 2)) {
                    return {ErrorCode::InvalidValue, id,
                            "Healing use effects contain only effect, amount, and optional attribute."};
                }
                if (!jsonUse.find("amount").integer(amount) || amount <= 0 ||
                    amount > kMaxItemEffectAmount) {
                    return {ErrorCode::InvalidValue, id,
                            "Healing requires a positive bounded amount."};
                }
                if (jsonUse.find("attribute").valid() &&
                    (!jsonUse.find("attribute").string(attribute) ||
                     !isValidContentId(attribute) ||
                     !copyBounded(attribute, item.useAttribute))) {
                    return {ErrorCode::InvalidValue, id,
                            "Healing attribute must be a bounded stable content ID."};
                }
                item.useEffect = ItemUseEffect::Heal;
            } else if (effect == "shield") {
                if (!hasOnlyFields(jsonUse, kShieldFields, 2) ||
                    !jsonUse.find("durationSteps").integer(durationSteps) ||
                    durationSteps <= 0 ||
                    durationSteps > kMaxItemUseDurationSteps) {
                    return {ErrorCode::InvalidValue, id,
                            "Shield effects require a bounded duration in fixed steps."};
                }
                item.useEffect = ItemUseEffect::Shield;
            } else if (effect == "speed_boost" || effect == "jump_boost") {
                if (!hasOnlyFields(jsonUse, kTimedBoostFields, 3) ||
                    !jsonUse.find("amount").integer(amount) || amount <= 0 ||
                    amount > kMaxItemEffectAmount ||
                    !jsonUse.find("durationSteps").integer(durationSteps) ||
                    durationSteps <= 0 ||
                    durationSteps > kMaxItemUseDurationSteps) {
                    return {ErrorCode::InvalidValue, id,
                            "Timed boosts require a positive amount and bounded duration."};
                }
                item.useEffect = effect == "speed_boost"
                    ? ItemUseEffect::SpeedBoost : ItemUseEffect::JumpBoost;
            } else if (effect == "area_damage") {
                if (!hasOnlyFields(jsonUse, kAreaDamageFields, 3) ||
                    !jsonUse.find("amount").integer(amount) || amount <= 0 ||
                    amount > kMaxItemEffectAmount ||
                    !jsonUse.find("radius").integer(radius) || radius <= 0 ||
                    radius > std::numeric_limits<std::uint16_t>::max()) {
                    return {ErrorCode::InvalidValue, id,
                            "Area damage requires a positive amount and bounded radius."};
                }
                item.useEffect = ItemUseEffect::AreaDamage;
            } else {
                return {ErrorCode::InvalidValue, id,
                        "Item use effect is not supported by the compiled inventory module."};
            }
            item.useAmount = static_cast<std::int32_t>(amount);
            item.useDurationSteps = static_cast<std::uint16_t>(durationSteps);
            item.useRadius = static_cast<std::uint16_t>(radius);
        }
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
