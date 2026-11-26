#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace echo::editor {

enum class FieldKind { Attribute, Stalker, Objective, Script };

struct Field {
    FieldKind kind{};
    std::size_t first{};
    std::size_t second{};
    std::string label;
    std::string value;
    std::int64_t minimum{};
    std::int64_t maximum{};
};

class ContentModel {
public:
    [[nodiscard]] bool open(std::string_view source, std::string& error);
    [[nodiscard]] bool edit(std::size_t index, std::string_view value, std::string& error);
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] const std::vector<Field>& fields() const noexcept { return fields_; }

private:
    std::string source_;
    std::vector<Field> fields_;
};

} // namespace echo::editor
