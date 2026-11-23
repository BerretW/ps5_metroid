#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

struct AttributeDefinition {
    std::string_view id;
    std::int32_t baseValue{};
    std::int32_t minimum{};
    std::int32_t maximum{};
};

template <std::size_t AttributeCapacity, std::size_t ModifierCapacity>
class AttributeSet {
    static_assert(AttributeCapacity > 0 && ModifierCapacity > 0);

public:
    [[nodiscard]] Status configure(
        std::span<const AttributeDefinition> definitions) noexcept {
        if (definitions.empty() || definitions.size() > AttributeCapacity) {
            return {ErrorCode::CapacityExceeded, "attributes",
                    "Attribute definition count is empty or exceeds fixed capacity."};
        }
        std::array<Attribute, AttributeCapacity> candidate{};
        for (std::size_t index = 0; index < definitions.size(); ++index) {
            const AttributeDefinition& definition = definitions[index];
            if (!isValidContentId(definition.id) ||
                definition.id.size() >= candidate[index].id.size() ||
                definition.minimum > definition.baseValue ||
                definition.baseValue > definition.maximum) {
                return {ErrorCode::InvalidValue, definition.id,
                        "Attribute ID or bounded base value is invalid."};
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (std::string_view(candidate[previous].id.data()) == definition.id) {
                    return {ErrorCode::DuplicateId, definition.id,
                            "Attribute IDs must be unique."};
                }
            }
            Attribute& attribute = candidate[index];
            std::copy(definition.id.begin(), definition.id.end(), attribute.id.begin());
            attribute.baseValue = definition.baseValue;
            attribute.minimum = definition.minimum;
            attribute.maximum = definition.maximum;
        }
        attributes_ = candidate;
        modifiers_.fill({});
        count_ = definitions.size();
        return Status::success();
    }

    [[nodiscard]] Status value(std::string_view id, std::int32_t& output) const noexcept {
        const std::size_t index = find(id);
        if (index == count_) {
            return {ErrorCode::NotFound, id, "Attribute is not configured."};
        }
        std::int64_t result = attributes_[index].baseValue;
        for (const Modifier& modifier : modifiers_) {
            if (modifier.active &&
                std::string_view(modifier.attribute.data()) == id) {
                result += modifier.amount;
            }
        }
        if (result < attributes_[index].minimum ||
            result > attributes_[index].maximum) {
            return {ErrorCode::InvalidState, id,
                    "Configured modifiers produce an out-of-range attribute."};
        }
        output = static_cast<std::int32_t>(result);
        return Status::success();
    }

    [[nodiscard]] Status addModifier(std::string_view id, std::string_view source,
                                     std::int32_t amount) noexcept {
        const std::size_t index = find(id);
        if (index == count_ || !isValidContentId(source) || amount == 0) {
            return {ErrorCode::InvalidValue, id,
                    "Modifier requires a configured attribute, stable source ID, and nonzero amount."};
        }
        for (const Modifier& modifier : modifiers_) {
            if (modifier.active && modifier.attribute == attributes_[index].id &&
                std::string_view(modifier.source.data()) == source) {
                return {ErrorCode::DuplicateId, source,
                        "A modifier source may affect an attribute only once."};
            }
        }
        std::int32_t current{};
        const Status currentStatus = value(id, current);
        if (!currentStatus) return currentStatus;
        const std::int64_t updated = static_cast<std::int64_t>(current) + amount;
        if (updated < attributes_[index].minimum ||
            updated > attributes_[index].maximum) {
            return {ErrorCode::InvalidValue, id,
                    "Modifier would place the attribute outside its declared bounds."};
        }
        const auto free = std::find_if(modifiers_.begin(), modifiers_.end(),
            [](const Modifier& modifier) { return !modifier.active; });
        if (free == modifiers_.end()) {
            return {ErrorCode::CapacityExceeded, "attributes.modifiers",
                    "Attribute modifier capacity is full."};
        }
        std::copy(id.begin(), id.end(), free->attribute.begin());
        std::copy(source.begin(), source.end(), free->source.begin());
        free->amount = amount;
        free->active = true;
        return Status::success();
    }

    [[nodiscard]] Status removeModifier(std::string_view id,
                                        std::string_view source) noexcept {
        const std::size_t index = find(id);
        if (index == count_) {
            return {ErrorCode::NotFound, id, "Attribute is not configured."};
        }
        const auto found = std::find_if(modifiers_.begin(), modifiers_.end(),
            [id, source](const Modifier& modifier) {
                return modifier.active &&
                       std::string_view(modifier.attribute.data()) == id &&
                       std::string_view(modifier.source.data()) == source;
            });
        if (found == modifiers_.end()) {
            return {ErrorCode::NotFound, source, "Attribute modifier is not active."};
        }
        std::int32_t current{};
        const Status currentStatus = value(id, current);
        if (!currentStatus) return currentStatus;
        const std::int64_t updated = static_cast<std::int64_t>(current) - found->amount;
        if (updated < attributes_[index].minimum ||
            updated > attributes_[index].maximum) {
            return {ErrorCode::InvalidState, id,
                    "Removing the modifier would violate the attribute bounds."};
        }
        *found = {};
        return Status::success();
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    struct Attribute {
        std::array<char, 128> id{};
        std::int32_t baseValue{};
        std::int32_t minimum{};
        std::int32_t maximum{};
    };

    struct Modifier {
        std::array<char, 128> attribute{};
        std::array<char, 128> source{};
        std::int32_t amount{};
        bool active{};
    };

    [[nodiscard]] std::size_t find(std::string_view id) const noexcept {
        for (std::size_t index = 0; index < count_; ++index) {
            if (std::string_view(attributes_[index].id.data()) == id) return index;
        }
        return count_;
    }

    std::array<Attribute, AttributeCapacity> attributes_{};
    std::array<Modifier, ModifierCapacity> modifiers_{};
    std::size_t count_{};
};

class AttributesModule final : public IModule {
public:
    static constexpr std::size_t kMaxDefinitions = 8;

    AttributesModule() noexcept = default;
    AttributesModule(const AttributesModule&) = delete;
    AttributesModule& operator=(const AttributesModule&) = delete;
    AttributesModule(AttributesModule&&) = delete;
    AttributesModule& operator=(AttributesModule&&) = delete;

    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] Status load(JsonValue document) noexcept;
    [[nodiscard]] std::span<const AttributeDefinition> definitions() const noexcept {
        return {definitions_.data(), definitionCount_};
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    struct StoredDefinition {
        std::array<char, 128> id{};
        std::int32_t baseValue{};
        std::int32_t minimum{};
        std::int32_t maximum{};
    };

    std::array<StoredDefinition, kMaxDefinitions> storedDefinitions_{};
    std::array<AttributeDefinition, kMaxDefinitions> definitions_{};
    std::size_t definitionCount_{};
    bool loaded_{};
    bool initialized_{};
};

[[nodiscard]] CapabilityManifestFragment attributesCapabilityManifestFragment() noexcept;

} // namespace echo::framework
