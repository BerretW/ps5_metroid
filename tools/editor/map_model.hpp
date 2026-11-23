#pragma once

#include "echo/framework/world.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace echo::editor {

struct MapLayer {
    std::string name;
    std::vector<std::string> rows;
};

class MapModel {
public:
    bool open(const std::wstring& filename, const std::string& source, std::string& error);
    bool paint(std::size_t layer, std::size_t column, std::size_t row, char symbol,
               std::string& error);
    bool undo();
    bool serialize(std::string& output, std::string& error) const;
    bool validate(const std::string& candidate, std::string& error) const;
    const std::vector<MapLayer>& layers() const { return layers_; }
    const framework::RoomDefinition* room() const { return world_ ? world_->findRoom(id_) : nullptr; }

private:
    bool loadWorld(const std::string& candidate, std::unique_ptr<framework::WorldModule>& world,
                   std::string& error) const;
    std::filesystem::path assets_;
    std::wstring filename_;
    std::string id_, source_;
    std::vector<MapLayer> layers_;
    struct Change { std::size_t layer, column, row; char previous; };
    std::vector<Change> history_;
    std::unique_ptr<framework::WorldModule> world_;
};

} // namespace echo::editor
