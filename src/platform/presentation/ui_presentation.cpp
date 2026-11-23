#include "platform/presentation/ui_presentation.hpp"

#include "platform/presentation/bitmap_text.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace echo::platform {
namespace {

constexpr rendering::Color2D kPanel{17, 29, 45, 255};
constexpr rendering::Color2D kBackground{7, 12, 22, 255};
constexpr rendering::Color2D kPanelLight{29, 48, 65, 255};
constexpr rendering::Color2D kText{220, 234, 239, 255};
constexpr rendering::Color2D kMuted{115, 145, 158, 255};
constexpr rendering::Color2D kCyan{61, 227, 216, 255};
constexpr rendering::Color2D kOrange{255, 164, 83, 255};

class Painter {
public:
    explicit Painter(rendering::ICanvas2D& canvas) noexcept : canvas_(canvas) {}

    void panel(rendering::Rect2D bounds, rendering::Color2D color) noexcept {
        if (ok_) ok_ = canvas_.fill(bounds, color);
    }

    void text(int x, int y, std::string_view value, int scale,
              rendering::Color2D color) noexcept {
        drawBitmapText(x, y, value, scale, [this, color](rendering::Rect2D bounds) noexcept {
            panel(bounds, color);
        });
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    rendering::ICanvas2D& canvas_;
    bool ok_{true};
};

} // namespace

bool drawMainMenu(rendering::ICanvas2D& canvas, ControlScheme controls, int selection,
                  bool continueAvailable, bool modelWindow) noexcept {
    Painter paint{canvas};
    for (int y = 34; y < 360; y += 32) {
        if (modelWindow && y >= 160 && y < 300) {
            paint.panel({0, y, 366, 1}, {18, 35, 48, 255});
            paint.panel({540, y, 100, 1}, {18, 35, 48, 255});
        } else {
            paint.panel({0, y, 640, 1}, {18, 35, 48, 255});
        }
    }
    if (modelWindow) {
        paint.panel({74, 62, 492, 98}, kPanel);
        paint.panel({74, 160, 292, 140}, kPanel);
        paint.panel({540, 160, 26, 140}, kPanel);
        paint.panel({74, 300, 492, 6}, kPanel);
    } else {
        paint.panel({74, 62, 492, 244}, kPanel);
    }
    paint.panel({74, 62, 492, 3}, kCyan);
    paint.text(120, 94, "PROJECT ECHO", 4, kText);
    paint.text(122, 135, "FACILITY 07  /  SIGNAL LOST", 1, kMuted);

    constexpr std::array<std::string_view, 5> labels{
        "NEW GAME", "CONTINUE", "OPTIONS", "DUALSENSE LAB", "EXIT"};
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        const int y = 169 + index * 27;
        if (index == selection) {
            paint.panel({112, y - 4, 230, 22}, kPanelLight);
            paint.panel({112, y - 5, 3, 24}, kCyan);
        }
        const auto color = index == 1 && !continueAvailable
                               ? kMuted : index == selection ? kCyan : kText;
        paint.text(128, y, labels[static_cast<std::size_t>(index)], index == 3 ? 1 : 2, color);
    }
    if (controls == ControlScheme::PlayStation) {
        paint.text(86, 326, "\x05 SELECT  \x01 CONFIRM", 2, kMuted);
    } else {
        paint.text(86, 326, "ARROWS SELECT  ENTER CONFIRM", 1, kMuted);
    }
    return paint.ok();
}

bool drawOptionsMenu(rendering::ICanvas2D& canvas, ControlScheme controls,
                     const OptionsPresentation& options) noexcept {
    Painter paint{canvas};
    paint.panel({74, 38, 492, 284}, kPanel);
    paint.panel({74, 38, 492, 3}, kCyan);
    paint.text(110, 62, "OPTIONS", 3, kText);
    if (controls == ControlScheme::PlayStation) {
        paint.text(110, 94, "FIXED CONTROLLER BUTTONS", 1, kMuted);
    }
    std::array<std::string_view, 8> rows{
        "VOLUME", "JUMP", "FIRE", "INTERACT", "DIALOGUE", "DISPLAY", "RENDER DEBUG", "BACK"};
    const int count = options.hasDisplayOption ? 8 : 7;
    const int debugOption = options.hasDisplayOption ? 6 : 5;
    rows[static_cast<std::size_t>(debugOption)] = "RENDER DEBUG";
    rows[static_cast<std::size_t>(count - 1)] = "BACK";
    for (int index = 0; index < count; ++index) {
        const int y = 110 + index * 22;
        if (options.selection == index) {
            paint.panel({102, y - 3, 436, 21}, kPanelLight);
            paint.panel({102, y - 3, 3, 21}, kCyan);
        }
        paint.text(118, y, rows[static_cast<std::size_t>(index)], 2,
                   options.selection == index ? kCyan : kText);
        if (index == 0) {
            std::array<char, 16> percentage{};
            std::snprintf(percentage.data(), percentage.size(), "%d%%", options.volume);
            paint.text(390, y, percentage.data(), 2, kText);
        } else if (index < 4) {
            const std::string_view keyName = controls == ControlScheme::PlayStation
                ? (index == 1 ? "\x01" : index == 2 ? "\x02" : "\x03")
                : (index == 1 ? options.jumpKey : index == 2 ? options.shootKey
                                                                  : options.interactKey);
            paint.text(390, y, keyName, 2, kText);
        } else if (index == 4) {
            paint.text(390, y, options.language == framework::DialogueLanguage::Czech ? "CS" : "EN",
                       2, kText);
        } else if (index == 5 && options.hasDisplayOption) {
            paint.text(390, y, options.display == DisplayMode::Fullscreen ? "FULLSCREEN"
                         : options.display == DisplayMode::Window1080 ? "1920X1080" : "1280X720",
                       2, kText);
        } else if (index == debugOption) {
            paint.text(390, y, options.renderDebug ? "ON" : "OFF", 2, kText);
        }
    }
    if (controls == ControlScheme::PlayStation) {
        paint.text(104, 294, "\x05 SELECT/CHANGE  \x04 BACK", 1, kMuted);
    } else {
        paint.text(104, 294, options.waitingForKey ? "PRESS A NEW KEY  ESC CANCEL"
                                                  : "ENTER REBIND  LEFT RIGHT CHANGE", 1, kMuted);
    }
    return paint.ok();
}

bool drawInventoryMenu(rendering::ICanvas2D& canvas, ControlScheme controls,
                       const Game& game, std::size_t selected, std::size_t source,
                       std::string_view message) noexcept {
    if (selected >= Game::kInventorySlotCount || source > Game::kInventorySlotCount) return false;
    Painter paint{canvas};
    paint.panel({16, 16, 608, 328}, kPanel);
    paint.panel({16, 16, 608, 3}, kCyan);
    paint.text(28, 28, "INVENTORY", 2, kText);
    paint.text(230, 30, Game::itemName(game.inventorySlot(selected).item), 1, kCyan);
    paint.text(28, 54, "BACKPACK 20 SLOTS", 1, kMuted);
    for (std::size_t slot = 0; slot < Game::kInventorySlotCount; ++slot) {
        const int column = static_cast<int>(slot % 5);
        const int row = static_cast<int>(slot / 5);
        const int x = 28 + column * 116;
        const int y = row < 4 ? 72 + row * 42 : 264;
        if (row == 4) paint.text(x, 247, Game::inventorySlotName(slot), 1, kMuted);
        const auto border = slot == selected ? kCyan : slot == source ? kOrange : kPanelLight;
        paint.panel({x, y, 108, 36}, border);
        paint.panel({x + 2, y + 2, 104, 32}, kBackground);
        const ItemStack stack = game.inventorySlot(slot);
        paint.text(x + 5, y + 6, Game::itemName(stack.item), 1,
                   stack.item == ItemId::None ? kMuted : kText);
        std::array<char, 32> quantity{};
        std::snprintf(quantity.data(), quantity.size(), "%02u QTY %u",
                      static_cast<unsigned>(slot + 1), static_cast<unsigned>(stack.quantity));
        paint.text(x + 5, y + 22, quantity.data(), 1, slot == source ? kOrange : kMuted);
    }
    paint.text(28, 311, {message.data(), std::min<std::size_t>(message.size(), 94)}, 1, kOrange);
    std::string_view effect{};
    switch (game.inventorySlot(selected).item) {
    case ItemId::JumpVisor: effect = "HEAD: JUMP +40"; break;
    case ItemId::VitalVest: effect = "BODY: MAX HEALTH +1"; break;
    case ItemId::PowerBarrel: effect = "WEAPON: SHOT DAMAGE +1"; break;
    case ItemId::SprintModule: effect = "ACCESSORY: SPEED +15"; break;
    case ItemId::MedPatch: effect = "ACTIVE: HEAL 1"; break;
    case ItemId::MedKit: effect = "ACTIVE: HEAL 4"; break;
    case ItemId::ShieldCell: effect = "ACTIVE: SHIELD 4 SECONDS"; break;
    case ItemId::SpeedCharge: effect = "ACTIVE: SPEED +25 FOR 8 SECONDS"; break;
    case ItemId::JumpCharge: effect = "ACTIVE: JUMP +60 FOR 8 SECONDS"; break;
    case ItemId::BlastCharge: effect = "ACTIVE: NEARBY ENEMIES TAKE 2 DAMAGE"; break;
    default: break;
    }
    paint.text(28, 321, effect, 1, kCyan);
    paint.text(28, 333, controls == ControlScheme::PlayStation
                           ? "\x05 SELECT  \x01 PICK/MOVE  \x04 CANCEL/BACK"
                           : "ARROWS SELECT  ENTER PICK/MOVE  ESC CANCEL/BACK", 1, kMuted);
    return paint.ok();
}

} // namespace echo::platform
