#pragma once

#include "game/game.hpp"

namespace echo::platform {

enum class InventoryCommand { Left, Right, Up, Down, Confirm, Back };

struct InventoryMenu {
    static constexpr std::size_t kNoSource = Game::kInventorySlotCount;
    std::size_t selected{};
    std::size_t source{kNoSource};
    bool returnToPause{};

    void reset(bool wasPaused = false) noexcept {
        selected = 0;
        source = kNoSource;
        returnToPause = wasPaused;
    }

    void navigate(int horizontal, int vertical) noexcept {
        constexpr int columns = 5;
        constexpr int rows = 5;
        const int column = (static_cast<int>(selected % columns) + horizontal + columns) % columns;
        const int row = (static_cast<int>(selected / columns) + vertical + rows) % rows;
        selected = static_cast<std::size_t>(row * columns + column);
    }

    [[nodiscard]] framework::Status confirm(Game& game) noexcept {
        if (source == kNoSource) {
            if (game.inventorySlot(selected).item == ItemId::None) {
                return {framework::ErrorCode::InvalidValue, "inventory.slot",
                        "Select a slot containing an item."};
            }
            source = selected;
            return framework::Status::success();
        }
        const framework::Status status = game.moveInventorySlot(source, selected);
        if (status) source = kNoSource;
        return status;
    }

    [[nodiscard]] bool back() noexcept {
        if (source == kNoSource) return true;
        source = kNoSource;
        return false;
    }
};

} // namespace echo::platform
