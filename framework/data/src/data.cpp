#include "echo/framework/data.hpp"

#include <yyjson.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>

namespace echo::framework {
namespace {

[[nodiscard]] std::string_view textOf(const yyjson_val* value) noexcept {
    if (value == nullptr || !yyjson_is_str(value)) {
        return {};
    }
    return {yyjson_get_str(value), yyjson_get_len(value)};
}

[[nodiscard]] bool fieldString(const yyjson_val* object, const char* name,
                               std::string_view& output) noexcept {
    const yyjson_val* field = yyjson_obj_get(object, name);
    if (field == nullptr || !yyjson_is_str(field)) {
        return false;
    }
    output = textOf(field);
    return true;
}

[[nodiscard]] bool fieldBool(const yyjson_val* object, const char* name,
                             bool& output) noexcept {
    const yyjson_val* field = yyjson_obj_get(object, name);
    if (field == nullptr || !yyjson_is_bool(field)) {
        return false;
    }
    output = yyjson_get_bool(field);
    return true;
}

[[nodiscard]] bool fieldInteger(const yyjson_val* object, const char* name,
                                std::int64_t& output) noexcept {
    const yyjson_val* field = yyjson_obj_get(object, name);
    if (field == nullptr || !yyjson_is_int(field)) {
        return false;
    }
    output = yyjson_get_sint(field);
    return true;
}

[[nodiscard]] bool validContentId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 127) {
        return false;
    }
    const std::size_t colon = id.find(':');
    const std::size_t slash = id.find('/');
    if (colon == std::string_view::npos || slash == std::string_view::npos ||
        colon == 0 || slash <= colon + 1 || slash + 1 >= id.size()) {
        return false;
    }
    for (std::size_t index = 0; index < id.size(); ++index) {
        const char character = id[index];
        const bool validCharacter = (character >= 'a' && character <= 'z') ||
                                    (character >= 'A' && character <= 'Z') ||
                                    (character >= '0' && character <= '9') ||
                                    character == '_' || character == '-' ||
                                    character == '.' || character == ':' || character == '/';
        if (!validCharacter || (character == ':' && index != colon) ||
            (character == '/' && index != slash)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool copyText(std::array<char, 128>& destination,
                            std::string_view source) noexcept {
    if (source.empty() || source.size() >= destination.size() || !validUtf8(source)) {
        return false;
    }
    destination.fill('\0');
    std::memcpy(destination.data(), source.data(), source.size());
    return true;
}

[[nodiscard]] bool copyText(std::array<char, 64>& destination,
                            std::string_view source) noexcept {
    if (source.empty() || source.size() >= destination.size() || !validUtf8(source)) {
        return false;
    }
    destination.fill('\0');
    std::memcpy(destination.data(), source.data(), source.size());
    return true;
}

[[nodiscard]] std::uint64_t integrityHash(const char* data, std::size_t size) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<unsigned char>(data[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::size_t findIntegrityMarker(const char* data, std::size_t size,
                                              bool& duplicated) noexcept {
    std::size_t found = size;
    std::size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t index = 0; index < size; ++index) {
        const char current = data[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (current == '\\') {
                escaped = true;
            } else if (current == '"') {
                inString = false;
            }
            continue;
        }
        if (current == '"') {
            if (depth == 1) {
                std::size_t previous = index;
                while (previous > 0 && (data[previous - 1] == ' ' || data[previous - 1] == '\t' ||
                                        data[previous - 1] == '\r' || data[previous - 1] == '\n')) {
                    --previous;
                }
                if (previous > 0 && (data[previous - 1] == '{' || data[previous - 1] == ',')) {
                    constexpr char key[] = "integrity";
                    const std::size_t keyStart = index + 1;
                    if (keyStart + sizeof(key) <= size &&
                        std::memcmp(data + keyStart, key, sizeof(key) - 1) == 0 &&
                        data[keyStart + sizeof(key) - 1] == '"') {
                        std::size_t colon = keyStart + sizeof(key);
                        while (colon < size && (data[colon] == ' ' || data[colon] == '\t' ||
                                                data[colon] == '\r' || data[colon] == '\n')) {
                            ++colon;
                        }
                        if (colon < size && data[colon] == ':') {
                            std::size_t marker = previous - 1;
                            if (data[marker] == ',') {
                                duplicated = found != size;
                                found = marker;
                            }
                        }
                    }
                }
            }
            inString = true;
        } else if (current == '{' || current == '[') {
            ++depth;
        } else if (current == '}' || current == ']') {
            if (depth != 0) {
                --depth;
            }
        }
    }
    return found;
}

class BoundedWriter;
[[nodiscard]] bool writeHash(BoundedWriter& writer, std::uint64_t hash) noexcept;

[[nodiscard]] bool resourceType(std::string_view text, ResourceType& type) noexcept {
    if (text == "generic") type = ResourceType::Generic;
    else if (text == "item") type = ResourceType::Item;
    else if (text == "ability") type = ResourceType::Ability;
    else if (text == "dialogue") type = ResourceType::Dialogue;
    else if (text == "cutscene") type = ResourceType::Cutscene;
    else if (text == "sprite") type = ResourceType::Sprite;
    else if (text == "sound") type = ResourceType::Sound;
    else return false;
    return true;
}

class BoundedWriter {
public:
    BoundedWriter(char* output, std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {}

    bool append(std::string_view text) noexcept {
        if (failed_ || text.size() > capacity_ - position_) {
            failed_ = true;
            return false;
        }
        std::memcpy(output_ + position_, text.data(), text.size());
        position_ += text.size();
        return true;
    }

    bool character(char value) noexcept {
        if (failed_ || position_ == capacity_) {
            failed_ = true;
            return false;
        }
        output_[position_++] = value;
        return true;
    }

    bool quoted(std::string_view value) noexcept {
        if (!validUtf8(value) || !character('"')) {
            return false;
        }
        static constexpr char hex[] = "0123456789abcdef";
        for (const unsigned char characterValue : value) {
            switch (characterValue) {
            case '"': if (!append("\\\"")) return false; break;
            case '\\': if (!append("\\\\")) return false; break;
            case '\b': if (!append("\\b")) return false; break;
            case '\f': if (!append("\\f")) return false; break;
            case '\n': if (!append("\\n")) return false; break;
            case '\r': if (!append("\\r")) return false; break;
            case '\t': if (!append("\\t")) return false; break;
            default:
                if (characterValue < 0x20U) {
                    if (!append("\\u00") || !character(hex[characterValue >> 4U]) ||
                        !character(hex[characterValue & 0x0fU])) {
                        return false;
                    }
                } else if (!character(static_cast<char>(characterValue))) {
                    return false;
                }
                break;
            }
        }
        return character('"');
    }

    bool unsignedInteger(std::uint64_t value) noexcept {
        char digits[24]{};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10U);
            value /= 10U;
        } while (value != 0);
        while (count != 0) {
            if (!character(digits[--count])) {
                return false;
            }
        }
        return true;
    }

    bool signedInteger(std::int64_t value) noexcept {
        if (value < 0 && !character('-')) {
            return false;
        }
        const std::uint64_t magnitude = value < 0
                                            ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                                            : static_cast<std::uint64_t>(value);
        return unsignedInteger(magnitude);
    }

    bool real(double value) noexcept {
        if (!std::isfinite(value)) {
            failed_ = true;
            return false;
        }
        char buffer[32]{};
        const int length = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        return length > 0 && static_cast<std::size_t>(length) < sizeof(buffer) &&
               append({buffer, static_cast<std::size_t>(length)});
    }

    [[nodiscard]] std::size_t size() const noexcept { return position_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    char* output_{};
    std::size_t capacity_{};
    std::size_t position_{};
    bool failed_{};
};

[[nodiscard]] bool writeHash(BoundedWriter& writer, std::uint64_t hash) noexcept {
    static constexpr char hex[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        if (!writer.character(hex[(hash >> shift) & 0x0fU])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view scopeName(StateScope scope) noexcept {
    switch (scope) {
    case StateScope::Global: return "global";
    case StateScope::World: return "world";
    case StateScope::Entity: return "entity";
    }
    return {};
}

[[nodiscard]] std::string_view valueTypeName(ValueType type) noexcept {
    switch (type) {
    case ValueType::Boolean: return "bool";
    case ValueType::Integer: return "int";
    case ValueType::Number: return "number";
    case ValueType::String: return "string";
    }
    return {};
}

[[nodiscard]] bool writeValue(BoundedWriter& writer, const StateValue& value) noexcept {
    switch (value.type) {
    case ValueType::Boolean: return writer.append(value.boolean ? "true" : "false");
    case ValueType::Integer: return writer.signedInteger(value.integer);
    case ValueType::Number: return writer.real(value.number);
    case ValueType::String: return writer.quoted(value.stringValue());
    }
    return false;
}

[[nodiscard]] bool slotCharacter(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_' || value == '-';
}

} // namespace

bool isValidContentId(std::string_view id) noexcept {
    return validContentId(id);
}

JsonDocument::~JsonDocument() {
    reset();
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept : document_(other.document_) {
    other.document_ = nullptr;
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
    if (this != &other) {
        reset();
        document_ = other.document_;
        other.document_ = nullptr;
    }
    return *this;
}

Status JsonDocument::parse(const char* data, std::size_t size,
                           JsonDocument& output, JsonError& error) noexcept {
    output.reset();
    error = {};
    if (data == nullptr || size == 0) {
        return {ErrorCode::InvalidArgument, {}, "JSON input must not be empty."};
    }
    if (size > kMaxJsonBytes) {
        return {ErrorCode::CapacityExceeded, {}, "JSON input exceeds the 1 MiB content limit."};
    }
    yyjson_read_err parseError{};
    // yyjson documents this cast as read-only when YYJSON_READ_INSITU is not set.
    yyjson_doc* document = yyjson_read_opts(const_cast<char*>(data), size,
                                             YYJSON_READ_NOFLAG, nullptr, &parseError);
    if (document == nullptr) {
        error.offset = parseError.pos;
        error.message = "Malformed JSON or invalid UTF-8.";
        return {ErrorCode::ParseError, {}, error.message};
    }
    output.document_ = document;
    return Status::success();
}

JsonValue JsonDocument::root() const noexcept {
    if (document_ == nullptr) {
        return JsonValue(nullptr);
    }
    return JsonValue(yyjson_doc_get_root(static_cast<const yyjson_doc*>(document_)));
}

void JsonDocument::reset() noexcept {
    if (document_ != nullptr) {
        yyjson_doc_free(static_cast<yyjson_doc*>(document_));
        document_ = nullptr;
    }
}

bool JsonValue::isObject() const noexcept {
    return value_ != nullptr && yyjson_is_obj(static_cast<const yyjson_val*>(value_));
}

bool JsonValue::isArray() const noexcept {
    return value_ != nullptr && yyjson_is_arr(static_cast<const yyjson_val*>(value_));
}

bool JsonValue::isString() const noexcept {
    return value_ != nullptr && yyjson_is_str(static_cast<const yyjson_val*>(value_));
}

bool JsonValue::isBoolean() const noexcept {
    return value_ != nullptr && yyjson_is_bool(static_cast<const yyjson_val*>(value_));
}

bool JsonValue::isInteger() const noexcept {
    return value_ != nullptr && yyjson_is_int(static_cast<const yyjson_val*>(value_));
}

bool JsonValue::isNumber() const noexcept {
    return value_ != nullptr && yyjson_is_num(static_cast<const yyjson_val*>(value_));
}

JsonValue JsonValue::find(std::string_view key) const noexcept {
    if (!isObject()) {
        return JsonValue(nullptr);
    }
    return JsonValue(yyjson_obj_getn(static_cast<const yyjson_val*>(value_), key.data(), key.size()));
}

std::size_t JsonValue::size() const noexcept {
    if (isArray()) {
        return yyjson_arr_size(static_cast<const yyjson_val*>(value_));
    }
    if (isObject()) {
        return yyjson_obj_size(static_cast<const yyjson_val*>(value_));
    }
    return 0;
}

JsonValue JsonValue::at(std::size_t index) const noexcept {
    if (!isArray()) {
        return JsonValue(nullptr);
    }
    return JsonValue(yyjson_arr_get(static_cast<const yyjson_val*>(value_), index));
}

bool JsonValue::memberAt(std::size_t index, std::string_view& key,
                         JsonValue& value) const noexcept {
    if (!isObject() || index >= yyjson_obj_size(static_cast<const yyjson_val*>(value_))) {
        return false;
    }
    yyjson_obj_iter iterator{};
    if (!yyjson_obj_iter_init(static_cast<const yyjson_val*>(value_), &iterator)) {
        return false;
    }
    yyjson_val* member = nullptr;
    for (std::size_t current = 0; current <= index; ++current) {
        member = yyjson_obj_iter_next(&iterator);
        if (member == nullptr) {
            return false;
        }
    }
    key = textOf(member);
    value = JsonValue(yyjson_obj_iter_get_val(member));
    return true;
}

bool JsonValue::string(std::string_view& value) const noexcept {
    if (!isString()) {
        return false;
    }
    value = textOf(static_cast<const yyjson_val*>(value_));
    return true;
}

bool JsonValue::boolean(bool& value) const noexcept {
    if (!isBoolean()) {
        return false;
    }
    value = yyjson_get_bool(static_cast<const yyjson_val*>(value_));
    return true;
}

bool JsonValue::integer(std::int64_t& value) const noexcept {
    if (!isInteger()) {
        return false;
    }
    value = yyjson_get_sint(static_cast<const yyjson_val*>(value_));
    return true;
}

bool JsonValue::number(double& value) const noexcept {
    if (!isNumber()) {
        return false;
    }
    value = yyjson_get_num(static_cast<const yyjson_val*>(value_));
    return std::isfinite(value);
}

Status JsonConditionTree::parse(JsonValue document) noexcept {
    reset();
    if (!document.isObject()) {
        return {ErrorCode::InvalidValue, {}, "Condition root must be an object."};
    }
    nodeCount_ = 1;
    const Status status = parseNode(document, 0, 0);
    if (!status) {
        reset();
        return status;
    }
    valid_ = true;
    return Status::success();
}

const Condition* JsonConditionTree::root() const noexcept {
    return valid_ ? &nodes_[0] : nullptr;
}

void JsonConditionTree::reset() noexcept {
    nodes_.fill(Condition{});
    for (auto& key : keys_) {
        key.fill('\0');
    }
    nodeCount_ = 0;
    valid_ = false;
}

Status JsonConditionTree::parseNode(JsonValue document, std::size_t nodeIndex,
                                    std::size_t depth) noexcept {
    if (depth >= 32) {
        return {ErrorCode::InvalidValue, "condition", "Condition nesting exceeds 32 levels."};
    }
    if (!document.isObject()) {
        return {ErrorCode::InvalidValue, "condition", "Each condition must be an object."};
    }
    std::string_view operation;
    if (!document.find("op").string(operation)) {
        return {ErrorCode::InvalidValue, "op", "Condition requires a string op field."};
    }

    if (operation == "and" || operation == "or") {
        const JsonValue children = document.find("conditions");
        if (!children.isArray() || children.size() == 0 ||
            children.size() > kMaxChildren) {
            return {ErrorCode::InvalidValue, "conditions",
                    "Logical conditions require 1 to 32 child conditions."};
        }
        if (children.size() > kMaxNodes - nodeCount_) {
            return {ErrorCode::CapacityExceeded, "conditions",
                    "Condition tree exceeds the 128-node limit."};
        }
        const std::size_t firstChild = nodeCount_;
        nodeCount_ += children.size();
        const std::span<const Condition> childSpan{nodes_.data() + firstChild, children.size()};
        nodes_[nodeIndex] = operation == "and" ? Condition::all(childSpan)
                                               : Condition::any(childSpan);
        for (std::size_t index = 0; index < children.size(); ++index) {
            const Status status = parseNode(children.at(index), firstChild + index, depth + 1);
            if (!status) {
                return status;
            }
        }
        return Status::success();
    }

    if (operation == "not") {
        const JsonValue child = document.find("condition");
        if (!child.valid()) {
            return {ErrorCode::InvalidValue, "condition", "NOT requires one condition field."};
        }
        if (nodeCount_ == kMaxNodes) {
            return {ErrorCode::CapacityExceeded, "condition",
                    "Condition tree exceeds the 128-node limit."};
        }
        const std::size_t childIndex = nodeCount_++;
        const Status status = parseNode(child, childIndex, depth + 1);
        if (!status) {
            return status;
        }
        nodes_[nodeIndex] = Condition::negate(nodes_[childIndex]);
        return Status::success();
    }

    if (operation != "exists" && operation != "compare") {
        return {ErrorCode::InvalidValue, "op", "Condition operation is not supported."};
    }

    std::string_view scopeName;
    std::string_view keyName;
    if (!document.find("scope").string(scopeName) ||
        !document.find("key").string(keyName) ||
        !validNamespaceKey(keyName)) {
        return {ErrorCode::InvalidValue, "key",
                "Leaf conditions require a valid scope and namespaced key."};
    }
    StateScope scope{};
    if (scopeName == "global") {
        scope = StateScope::Global;
    } else if (scopeName == "world") {
        scope = StateScope::World;
    } else if (scopeName == "entity") {
        scope = StateScope::Entity;
    } else {
        return {ErrorCode::InvalidValue, "scope", "Condition scope is not supported."};
    }

    EntityId entity{};
    const JsonValue entityField = document.find("entity");
    if (scope == StateScope::Entity) {
        std::int64_t index{};
        std::int64_t generation{};
        if (!entityField.isObject() ||
            !entityField.find("index").integer(index) ||
            !entityField.find("generation").integer(generation) ||
            index < 0 || index >= EntityId::kInvalidIndex || generation <= 0 ||
            generation > std::numeric_limits<std::uint32_t>::max()) {
            return {ErrorCode::InvalidValue, "entity",
                    "Entity scope requires valid unsigned index and generation fields."};
        }
        entity = {static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation)};
    } else if (entityField.valid()) {
        return {ErrorCode::InvalidValue, "entity",
                "Only entity-scoped conditions may include an entity field."};
    }

    auto& ownedKey = keys_[nodeIndex];
    if (keyName.empty() || keyName.size() >= ownedKey.size()) {
        return {ErrorCode::InvalidValue, "key", "Condition key exceeds 63 bytes."};
    }
    std::memcpy(ownedKey.data(), keyName.data(), keyName.size());
    const StateAddress address{scope, entity, {ownedKey.data(), keyName.size()}};
    if (operation == "exists") {
        nodes_[nodeIndex] = Condition::exists(address);
        return Status::success();
    }

    std::string_view comparisonName;
    if (!document.find("comparison").string(comparisonName)) {
        return {ErrorCode::InvalidValue, "comparison",
                "Comparison conditions require a string comparison field."};
    }
    CompareOperator comparison{};
    if (comparisonName == "eq") {
        comparison = CompareOperator::Equal;
    } else if (comparisonName == "ne") {
        comparison = CompareOperator::NotEqual;
    } else if (comparisonName == "lt") {
        comparison = CompareOperator::Less;
    } else if (comparisonName == "lte") {
        comparison = CompareOperator::LessOrEqual;
    } else if (comparisonName == "gt") {
        comparison = CompareOperator::Greater;
    } else if (comparisonName == "gte") {
        comparison = CompareOperator::GreaterOrEqual;
    } else {
        return {ErrorCode::InvalidValue, "comparison",
                "Comparison operator is not supported."};
    }

    const JsonValue jsonValue = document.find("value");
    StateValue value{};
    if (jsonValue.isBoolean()) {
        bool boolean{};
        (void)jsonValue.boolean(boolean);
        value = StateValue::fromBoolean(boolean);
    } else if (jsonValue.isInteger()) {
        std::int64_t integer{};
        (void)jsonValue.integer(integer);
        value = StateValue::fromInteger(integer);
    } else if (jsonValue.isNumber()) {
        double number{};
        if (!jsonValue.number(number)) {
            return {ErrorCode::InvalidValue, "value", "Comparison number must be finite."};
        }
        value = StateValue::fromNumber(number);
    } else if (jsonValue.isString()) {
        std::string_view string;
        (void)jsonValue.string(string);
        if (!StateValue::fromString(string, value)) {
            return {ErrorCode::InvalidValue, "value",
                    "Comparison string exceeds the state value limit."};
        }
    } else {
        return {ErrorCode::InvalidValue, "value",
                "Comparison value must be a boolean, number, integer, or string."};
    }
    if (comparison != CompareOperator::Equal && comparison != CompareOperator::NotEqual &&
        value.type != ValueType::Integer && value.type != ValueType::Number) {
        return {ErrorCode::InvalidValue, "value",
                "Ordering comparisons require an integer or number value."};
    }
    nodes_[nodeIndex] = Condition::compare(address, comparison, value);
    return Status::success();
}

Status ContentRegistry::add(const ResourceSpec& resource) noexcept {
    if (!validContentId(resource.id) || resource.dependencies.size() > kMaxDependencies) {
        return {ErrorCode::InvalidValue, resource.id, "Resource ID or dependency count is invalid."};
    }
    for (const Resource& current : resources_) {
        if (current.active && std::string_view(current.id.data()) == resource.id) {
            return {ErrorCode::DuplicateId, resource.id, "Resource ID is already registered."};
        }
    }
    if (count_ == resources_.size()) {
        return {ErrorCode::CapacityExceeded, resource.id, "Content registry capacity is full."};
    }
    Resource item{};
    if (!copyText(item.id, resource.id)) {
        return {ErrorCode::InvalidValue, resource.id, "Resource ID is too long or invalid UTF-8."};
    }
    item.type = resource.type;
    item.optional = resource.optional;
    for (const ContentDependency& dependency : resource.dependencies) {
        if (!validContentId(dependency.id)) {
            return {ErrorCode::InvalidValue, dependency.id, "Dependency ID is invalid."};
        }
        Dependency& destination = item.dependencies[item.dependencyCount++];
        if (!copyText(destination.id, dependency.id)) {
            return {ErrorCode::InvalidValue, dependency.id, "Dependency ID is too long or invalid UTF-8."};
        }
        destination.required = dependency.required;
    }
    item.active = true;
    for (Resource& slot : resources_) {
        if (!slot.active) {
            slot = item;
            ++count_;
            return Status::success();
        }
    }
    return {ErrorCode::CapacityExceeded, resource.id, "Content registry has no free resource slot."};
}

Status ContentRegistry::find(std::string_view id, ResourceInfo& resource) const noexcept {
    for (const Resource& item : resources_) {
        if (item.active && std::string_view(item.id.data()) == id) {
            resource = {std::string_view(item.id.data()), item.type, item.optional};
            return Status::success();
        }
    }
    return {ErrorCode::NotFound, id, "Resource ID is not registered."};
}

Status ContentRegistry::validate() const noexcept {
    std::array<std::uint8_t, kMaxResources> marks{};
    for (std::size_t index = 0; index < resources_.size(); ++index) {
        if (!resources_[index].active) {
            continue;
        }
        for (std::size_t dependencyIndex = 0;
             dependencyIndex < resources_[index].dependencyCount; ++dependencyIndex) {
            const Dependency& dependency = resources_[index].dependencies[dependencyIndex];
            bool found = false;
            for (const Resource& candidate : resources_) {
                found = found || (candidate.active &&
                                  std::string_view(candidate.id.data()) == dependency.id.data());
            }
            if (!found && dependency.required) {
                return {ErrorCode::MissingDependency, dependency.id.data(),
                        "A resource requires a missing content ID."};
            }
        }
    }
    const auto visit = [this, &marks](auto&& self, std::size_t index) noexcept -> Status {
        if (marks[index] == 2) {
            return Status::success();
        }
        if (marks[index] == 1) {
            return {ErrorCode::DependencyCycle, resources_[index].id.data(),
                    "Content dependency cycle detected."};
        }
        marks[index] = 1;
        const Resource& resource = resources_[index];
        for (std::size_t dependencyIndex = 0;
             dependencyIndex < resource.dependencyCount; ++dependencyIndex) {
            const Dependency& dependency = resource.dependencies[dependencyIndex];
            if (!dependency.required) {
                continue;
            }
            for (std::size_t candidateIndex = 0; candidateIndex < resources_.size(); ++candidateIndex) {
                if (resources_[candidateIndex].active &&
                    std::string_view(resources_[candidateIndex].id.data()) == dependency.id.data()) {
                    const Status status = self(self, candidateIndex);
                    if (!status) {
                        return status;
                    }
                    break;
                }
            }
        }
        marks[index] = 2;
        return Status::success();
    };
    for (std::size_t index = 0; index < resources_.size(); ++index) {
        if (resources_[index].active) {
            const Status status = visit(visit, index);
            if (!status) {
                return status;
            }
        }
    }
    return Status::success();
}

Status loadResourceManifest(JsonValue document, ContentRegistry& output) noexcept {
    if (!document.isObject()) {
        return {ErrorCode::InvalidValue, {}, "Content manifest root must be an object."};
    }
    const JsonValue resources = document.find("resources");
    if (!resources.isArray() || resources.size() > ContentRegistry::kMaxResources) {
        return {ErrorCode::InvalidValue, "resources",
                "Manifest requires a bounded resources array."};
    }
    ContentRegistry candidate{};
    std::array<ContentDependency, ContentRegistry::kMaxDependencies> dependencies{};
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const JsonValue item = resources.at(index);
        if (!item.isObject()) {
            return {ErrorCode::InvalidValue, "resources", "Each resource must be an object."};
        }
        std::string_view id;
        std::string_view typeName;
        if (!item.find("id").string(id) || !item.find("type").string(typeName)) {
            return {ErrorCode::InvalidValue, "resources", "Each resource needs string id and type fields."};
        }
        ResourceType type{};
        if (!resourceType(typeName, type)) {
            return {ErrorCode::InvalidValue, typeName, "Resource type is not supported."};
        }
        bool optional = false;
        const JsonValue optionalField = item.find("optional");
        if (optionalField.valid() && !optionalField.boolean(optional)) {
            return {ErrorCode::InvalidValue, "optional", "Resource optional field must be boolean."};
        }
        std::size_t dependencyCount = 0;
        for (const auto& field : {std::pair{"dependsOn", true},
                                  std::pair{"optionalDependencies", false}}) {
            const JsonValue dependencyField = item.find(field.first);
            if (!dependencyField.valid()) {
                continue;
            }
            if (!dependencyField.isArray() ||
                dependencyField.size() > ContentRegistry::kMaxDependencies - dependencyCount) {
                return {ErrorCode::InvalidValue, field.first,
                        "Dependency lists must fit the bounded resource limit."};
            }
            for (std::size_t dependencyIndex = 0;
                 dependencyIndex < dependencyField.size(); ++dependencyIndex) {
                if (!dependencyField.at(dependencyIndex).string(dependencies[dependencyCount].id)) {
                    return {ErrorCode::InvalidValue, field.first, "Dependency IDs must be strings."};
                }
                dependencies[dependencyCount++].required = field.second;
            }
        }
        const Status added = candidate.add({id, type, optional,
                                            {dependencies.data(), dependencyCount}});
        if (!added) {
            return added;
        }
    }
    const Status validation = candidate.validate();
    if (!validation) {
        return {validation.code, "resources", validation.detail};
    }
    output = candidate;
    return Status::success();
}

Status SaveCodec::encode(std::string_view gameId, std::uint32_t formatVersion,
                         std::span<const ModuleVersion> modules,
                         const GameState& state, char* output,
                         std::size_t capacity, std::size_t& written) noexcept {
    written = 0;
    if (output == nullptr || capacity == 0 || !validNamespaceKey(gameId) ||
        modules.size() > 16 || formatVersion == 0) {
        return {ErrorCode::InvalidArgument, gameId, "Save metadata or output buffer is invalid."};
    }
    if (capacity > kMaxSaveBytes) {
        capacity = kMaxSaveBytes;
    }
    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (!validNamespaceKey(modules[index].id) || modules[index].version == 0) {
            return {ErrorCode::InvalidValue, modules[index].id, "Save module metadata is invalid."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (modules[index].id == modules[previous].id) {
                return {ErrorCode::DuplicateId, modules[index].id, "Save module ID is duplicated."};
            }
        }
    }
    BoundedWriter writer(output, capacity);
    writer.append("{\"format\":");
    writer.unsignedInteger(formatVersion);
    writer.append(",\"game\":");
    writer.quoted(gameId);
    writer.append(",\"modules\":[");
    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (index != 0) writer.character(',');
        writer.append("{\"id\":");
        writer.quoted(modules[index].id);
        writer.append(",\"version\":");
        writer.unsignedInteger(modules[index].version);
        writer.append(",\"required\":");
        writer.append(modules[index].required ? "true" : "false");
        writer.character('}');
    }
    writer.append("],\"state\":[");
    for (std::size_t index = 0; index < state.size(); ++index) {
        StateEntryView entry{};
        if (!state.entryAt(index, entry)) {
            return {ErrorCode::InvalidState, {}, "State snapshot changed during serialization."};
        }
        if (index != 0) writer.character(',');
        writer.append("{\"scope\":");
        writer.quoted(scopeName(entry.address.scope));
        if (entry.address.scope == StateScope::Entity) {
            writer.append(",\"entityIndex\":");
            writer.unsignedInteger(entry.address.entity.index);
            writer.append(",\"entityGeneration\":");
            writer.unsignedInteger(entry.address.entity.generation);
        }
        writer.append(",\"key\":");
        writer.quoted(entry.address.key);
        writer.append(",\"type\":");
        writer.quoted(valueTypeName(entry.value.type));
        writer.append(",\"value\":");
        if (!writeValue(writer, entry.value)) {
            return {ErrorCode::InvalidValue, entry.address.key, "State value cannot be serialized."};
        }
        writer.character('}');
    }
    writer.character(']');
    const std::uint64_t digest = integrityHash(output, writer.size());
    writer.append(",\"integrity\":\"");
    if (!writeHash(writer, digest)) {
        return {ErrorCode::BufferTooSmall, {}, "Save checksum exceeds the output capacity."};
    }
    writer.append("\"}");
    if (writer.failed()) {
        return {ErrorCode::BufferTooSmall, {}, "Save exceeds the output capacity or 64 KiB limit."};
    }
    written = writer.size();
    return Status::success();
}

Status SaveCodec::decode(const char* data, std::size_t size,
                         std::string_view expectedGameId,
                         std::uint32_t currentFormatVersion,
                         std::span<const ModuleVersion> expectedModules,
                         GameState& output, SaveHeader& header,
                         SaveMigration migration, void* migrationContext) noexcept {
    if (size > kMaxSaveBytes || currentFormatVersion == 0 ||
        !validNamespaceKey(expectedGameId)) {
        return {ErrorCode::InvalidArgument, expectedGameId, "Save size or expected metadata is invalid."};
    }
    constexpr char marker[] = ",\"integrity\":\"";
    bool duplicateIntegrity = false;
    const std::size_t markerPosition = data == nullptr ? size
                                                       : findIntegrityMarker(data, size, duplicateIntegrity);
    const std::size_t hashStart = markerPosition + sizeof(marker) - 1;
    if (markerPosition == size || duplicateIntegrity || size != hashStart + 18 ||
        data[hashStart + 16] != '"' || data[hashStart + 17] != '}') {
        return {ErrorCode::CorruptData, {}, "Save integrity field is missing or malformed."};
    }
    char expectedHash[17]{};
    std::memcpy(expectedHash, data + hashStart, 16);
    char actualHash[17]{};
    BoundedWriter hashWriter(actualHash, 16);
    if (!writeHash(hashWriter, integrityHash(data, markerPosition)) ||
        std::string_view(expectedHash, 16) != std::string_view(actualHash, 16)) {
        return {ErrorCode::CorruptData, {}, "Save integrity check failed."};
    }
    JsonDocument document{};
    JsonError parseError{};
    const Status parsed = JsonDocument::parse(data, size, document, parseError);
    if (!parsed) {
        return parsed;
    }
    const JsonValue root = document.root();
    std::int64_t version{};
    std::string_view gameId;
    const JsonValue moduleArray = root.find("modules");
    const JsonValue stateArray = root.find("state");
    if (!root.isObject() || !root.find("format").integer(version) || version <= 0 ||
        version > std::numeric_limits<std::uint32_t>::max() ||
        !root.find("game").string(gameId) || !moduleArray.isArray() || moduleArray.size() > 16 ||
        !stateArray.isArray() || stateArray.size() > GameState::kMaxEntries) {
        return {ErrorCode::CorruptData, {}, "Save is missing required fields or exceeds schema limits."};
    }
    if (gameId != expectedGameId) {
        return {ErrorCode::InvalidValue, "game", "Save belongs to a different game identifier."};
    }
    SaveHeader candidateHeader{};
    if (!copyText(candidateHeader.gameId, gameId)) {
        return {ErrorCode::CorruptData, "game", "Game ID is too long or invalid UTF-8."};
    }
    candidateHeader.formatVersion = static_cast<std::uint32_t>(version);
    candidateHeader.moduleCount = moduleArray.size();
    for (std::size_t index = 0; index < moduleArray.size(); ++index) {
        const JsonValue item = moduleArray.at(index);
        std::string_view id;
        std::int64_t moduleVersion{};
        if (!item.isObject() || !item.find("id").string(id) ||
            !item.find("version").integer(moduleVersion) || moduleVersion <= 0 ||
            moduleVersion > std::numeric_limits<std::uint32_t>::max() ||
            !item.find("required").isBoolean() || !copyText(candidateHeader.moduleIds[index], id) ||
            !validNamespaceKey(id)) {
            return {ErrorCode::CorruptData, "modules", "Save module metadata is invalid."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (id == candidateHeader.moduleIds[previous].data()) {
                return {ErrorCode::CorruptData, "modules", "Save contains duplicate module IDs."};
            }
        }
        candidateHeader.moduleVersions[index] = static_cast<std::uint32_t>(moduleVersion);
    }
    for (const ModuleVersion& expected : expectedModules) {
        bool found = false;
        for (std::size_t index = 0; index < candidateHeader.moduleCount; ++index) {
            if (expected.id == candidateHeader.moduleIds[index].data()) {
                found = true;
                if (expected.version != candidateHeader.moduleVersions[index]) {
                    return {ErrorCode::UnsupportedVersion, expected.id,
                            "A saved module version does not match the enabled game module."};
                }
                break;
            }
        }
        if (!found && expected.required) {
            return {ErrorCode::UnsupportedVersion, expected.id,
                    "A required module is missing from this save."};
        }
    }

    GameState candidate{};
    for (std::size_t index = 0; index < stateArray.size(); ++index) {
        const JsonValue item = stateArray.at(index);
        std::string_view scopeText;
        std::string_view key;
        std::string_view typeText;
        if (!item.isObject() || !item.find("scope").string(scopeText) ||
            !item.find("key").string(key) || !item.find("type").string(typeText)) {
            return {ErrorCode::CorruptData, "state", "Save state entry is malformed."};
        }
        StateAddress address{};
        address.key = key;
        if (scopeText == "global") {
            address.scope = StateScope::Global;
        } else if (scopeText == "world") {
            address.scope = StateScope::World;
        } else if (scopeText == "entity") {
            std::int64_t entityIndex{};
            std::int64_t generation{};
            if (!item.find("entityIndex").integer(entityIndex) || entityIndex < 0 ||
                entityIndex >= EntityId::kInvalidIndex ||
                !item.find("entityGeneration").integer(generation) || generation <= 0 ||
                generation > std::numeric_limits<std::uint32_t>::max()) {
                return {ErrorCode::CorruptData, "state", "Entity state handle is invalid."};
            }
            address.scope = StateScope::Entity;
            address.entity = {static_cast<std::uint32_t>(entityIndex),
                              static_cast<std::uint32_t>(generation)};
        } else {
            return {ErrorCode::CorruptData, "scope", "Save contains an unknown state scope."};
        }

        const JsonValue stored = item.find("value");
        StateValue value{};
        if (typeText == "bool") {
            bool boolean{};
            if (!stored.boolean(boolean)) return {ErrorCode::CorruptData, "state", "Boolean state value is invalid."};
            value = StateValue::fromBoolean(boolean);
        } else if (typeText == "int") {
            std::int64_t integer{};
            if (!stored.integer(integer)) return {ErrorCode::CorruptData, "state", "Integer state value is invalid."};
            value = StateValue::fromInteger(integer);
        } else if (typeText == "number") {
            double number{};
            if (!stored.number(number)) return {ErrorCode::CorruptData, "state", "Numeric state value is invalid."};
            value = StateValue::fromNumber(number);
        } else if (typeText == "string") {
            std::string_view text;
            if (!stored.string(text) || !StateValue::fromString(text, value)) {
                return {ErrorCode::CorruptData, "state", "String state value is too long or invalid UTF-8."};
            }
        } else {
            return {ErrorCode::CorruptData, "type", "Save contains an unknown state value type."};
        }
        const Status storedStatus = candidate.set(address, value);
        if (!storedStatus) {
            return {ErrorCode::CorruptData, "state", storedStatus.detail};
        }
    }

    if (candidateHeader.formatVersion > currentFormatVersion) {
        return {ErrorCode::UnsupportedVersion, {}, "Save format is newer than this framework."};
    }
    if (candidateHeader.formatVersion != currentFormatVersion) {
        if (migration == nullptr ||
            !migration(candidateHeader.formatVersion, candidate, migrationContext)) {
            return {ErrorCode::UnsupportedVersion, {}, "No compatible save migration is registered."};
        }
        candidateHeader.formatVersion = currentFormatVersion;
    }
    const Status replaced = output.replaceValues(candidate);
    if (!replaced) {
        return replaced;
    }
    header = candidateHeader;
    return Status::success();
}

Status SaveService::save(const SaveStorage& storage, std::string_view slot,
                         std::string_view gameId, std::uint32_t formatVersion,
                         std::span<const ModuleVersion> modules,
                         const GameState& state, char* scratch,
                         std::size_t scratchCapacity) noexcept {
    if (!validSlot(slot) || storage.writeTemporary == nullptr || storage.replace == nullptr ||
        storage.removeTemporary == nullptr || scratch == nullptr) {
        return {ErrorCode::InvalidArgument, slot, "Save slot, storage callbacks, or scratch buffer is invalid."};
    }
    std::size_t written{};
    const Status encoded = SaveCodec::encode(gameId, formatVersion, modules, state,
                                             scratch, scratchCapacity, written);
    if (!encoded) {
        return encoded;
    }
    if (!storage.writeTemporary(storage.context, slot, scratch, written)) {
        return {ErrorCode::StorageFailure, slot, "Temporary save write failed."};
    }
    if (!storage.replace(storage.context, slot)) {
        const bool cleaned = storage.removeTemporary(storage.context, slot);
        return {ErrorCode::StorageFailure, slot, cleaned
                    ? "Save replacement failed; storage must preserve the previous valid slot."
                    : "Save replacement failed and temporary cleanup failed; previous slot must remain intact."};
    }
    return Status::success();
}

Status SaveService::load(const SaveStorage& storage, std::string_view slot,
                         std::string_view expectedGameId,
                         std::uint32_t currentFormatVersion,
                         std::span<const ModuleVersion> expectedModules,
                         GameState& output, SaveHeader& header,
                         char* scratch, std::size_t scratchCapacity,
                         SaveMigration migration, void* migrationContext) noexcept {
    if (!validSlot(slot) || storage.read == nullptr || scratch == nullptr ||
        scratchCapacity == 0 || scratchCapacity > kMaxSaveBytes) {
        return {ErrorCode::InvalidArgument, slot, "Load slot, storage callback, or buffer is invalid."};
    }
    std::size_t size{};
    if (!storage.read(storage.context, slot, scratch, scratchCapacity, size)) {
        return {ErrorCode::StorageFailure, slot, "Save slot could not be read."};
    }
    if (size == 0 || size > scratchCapacity || size > kMaxSaveBytes) {
        return {ErrorCode::CorruptData, slot, "Save slot has an invalid or excessive size."};
    }
    return SaveCodec::decode(scratch, size, expectedGameId, currentFormatVersion,
                             expectedModules, output, header, migration, migrationContext);
}

Status SaveService::enumerate(const SaveStorage& storage, char* output,
                              std::size_t capacity, std::size_t& written) noexcept {
    written = 0;
    if (storage.enumerate == nullptr || output == nullptr || capacity == 0 ||
        capacity > kMaxSaveBytes) {
        return {ErrorCode::InvalidArgument, {}, "Enumeration callback or output buffer is invalid."};
    }
    if (!storage.enumerate(storage.context, output, capacity, written) || written > capacity) {
        return {ErrorCode::StorageFailure, {}, "Save-slot enumeration failed."};
    }
    std::size_t offset = 0;
    while (offset < written) {
        const std::size_t remaining = written - offset;
        const void* terminator = std::memchr(output + offset, '\0', remaining);
        if (terminator == nullptr) {
            return {ErrorCode::CorruptData, {}, "Save-slot list is not NUL terminated."};
        }
        const auto* end = static_cast<const char*>(terminator);
        const std::string_view slot(output + offset, static_cast<std::size_t>(end - (output + offset)));
        if (!validSlot(slot)) {
            return {ErrorCode::CorruptData, slot, "Storage returned an invalid save-slot ID."};
        }
        offset += slot.size() + 1;
    }
    return Status::success();
}

bool SaveService::validSlot(std::string_view slot) noexcept {
    if (slot.empty() || slot.size() > 32) {
        return false;
    }
    for (const char character : slot) {
        if (!slotCharacter(character)) {
            return false;
        }
    }
    return true;
}

} // namespace echo::framework
