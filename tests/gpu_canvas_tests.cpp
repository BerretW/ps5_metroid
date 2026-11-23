#include "platform/presentation/gpu_canvas.hpp"
#include "platform/presentation/ui_presentation.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

extern "C" unsigned char* echo_decode_png_rgba(const unsigned char*, int, int* width,
                                                int* height) {
    *width = 1;
    *height = 1;
    return static_cast<unsigned char*>(std::calloc(4, 1));
}

extern "C" void echo_free_png(unsigned char* pixels) {
    std::free(pixels);
}

namespace {

using namespace echo::rendering;

class Backend final : public IRenderBackend {
public:
    int textures{};
    int materials{};
    int meshes{};
    int textureCalls{};
    int failTextureCall{};
    bool failMaterial{};
    bool failMesh{};

    Status initialize(const BackendInit&) noexcept override { return Status::success(); }
    Capabilities capabilities() const noexcept override { return {}; }
    Status createMesh(MeshView, MeshHandle&) noexcept override { return error(); }
    Status createDynamicMesh(std::size_t, std::size_t, MeshHandle& output) noexcept override {
        if (failMesh) return error();
        ++meshes;
        output = {0, 1};
        return Status::success();
    }
    Status updateDynamicMesh(MeshHandle, MeshView) noexcept override { return Status::success(); }
    Status createTexture(std::uint32_t, std::uint32_t, std::span<const std::byte>,
                         TextureHandle& output, TextureColorSpace) noexcept override {
        if (++textureCalls == failTextureCall) return error();
        ++textures;
        output = {static_cast<std::uint32_t>(textureCalls), 1};
        return Status::success();
    }
    Status createMaterial(const Material&, MaterialHandle& output) noexcept override {
        if (failMaterial) return error();
        ++materials;
        output = {0, 1};
        return Status::success();
    }
    Status destroyMesh(MeshHandle) noexcept override { --meshes; return Status::success(); }
    Status destroyTexture(TextureHandle) noexcept override { --textures; return Status::success(); }
    Status destroyMaterial(MaterialHandle) noexcept override { --materials; return Status::success(); }
    Status createTarget(std::uint32_t, std::uint32_t, TargetHandle&) noexcept override {
        return error();
    }
    Status beginFrame(const FrameState&) noexcept override { return error(); }
    Status submit(const Draw&) noexcept override { return error(); }
    Status endFrame() noexcept override { return error(); }
    Status resize(std::uint32_t, std::uint32_t) noexcept override { return error(); }
    Diagnostics diagnostics() const noexcept override { return {}; }
    void shutdown() noexcept override {}

private:
    static Status error() noexcept {
        return {ErrorCode::InvalidState, "test backend", "Injected failure."};
    }
};

class PixelCanvas final : public ICanvas2D {
public:
    std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(640 * 360);

    bool clear(Color2D color) noexcept override {
        std::fill(pixels.begin(), pixels.end(), pack(color));
        return true;
    }
    bool fill(Rect2D rect, Color2D color) noexcept override {
        for (int y = std::max(0, rect.y); y < std::min(360, rect.y + rect.h); ++y) {
            for (int x = std::max(0, rect.x); x < std::min(640, rect.x + rect.w); ++x) {
                pixels[static_cast<std::size_t>(y * 640 + x)] = pack(color);
            }
        }
        return true;
    }
    bool texture(TextureHandle, Rect2D, Vec4, Vec4, float) noexcept override { return false; }

    [[nodiscard]] std::uint32_t pixel(int x, int y) const noexcept {
        return pixels[static_cast<std::size_t>(y * 640 + x)];
    }

private:
    static std::uint32_t pack(Color2D color) noexcept {
        return static_cast<std::uint32_t>(color.r) << 24U |
               static_cast<std::uint32_t>(color.g) << 16U |
               static_cast<std::uint32_t>(color.b) << 8U | color.a;
    }
};

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    PixelCanvas menu;
    PixelCanvas changedMenu;
    check(echo::platform::drawMainMenu(menu, echo::platform::ControlScheme::Keyboard,
                                       0, true, false) &&
              echo::platform::drawMainMenu(changedMenu, echo::platform::ControlScheme::Keyboard,
                                           1, false, false) &&
              menu.pixel(113, 165) != changedMenu.pixel(113, 165) &&
              menu.pixel(113, 192) != changedMenu.pixel(113, 192),
          "SDL-free main menu highlights the selected row");
    echo::platform::OptionsPresentation options{};
    options.volume = 70;
    options.language = echo::framework::DialogueLanguage::Czech;
    options.hasDisplayOption = true;
    check(echo::platform::drawOptionsMenu(menu, echo::platform::ControlScheme::Keyboard, options) &&
              echo::platform::drawOptionsMenu(changedMenu,
                                               echo::platform::ControlScheme::PlayStation, options) &&
              menu.pixels != changedMenu.pixels,
          "SDL-free options draw different keyboard and controller prompts");
    echo::Game game;
    check(echo::platform::drawInventoryMenu(menu, echo::platform::ControlScheme::Keyboard,
                                             game, 0, echo::Game::kInventorySlotCount, {}) &&
              echo::platform::drawInventoryMenu(changedMenu,
                                                 echo::platform::ControlScheme::Keyboard,
                                                 game, 1, echo::Game::kInventorySlotCount, {}) &&
              menu.pixel(28, 72) != changedMenu.pixel(28, 72) &&
              !echo::platform::drawInventoryMenu(menu, echo::platform::ControlScheme::Keyboard,
                                                  game, echo::Game::kInventorySlotCount,
                                                  echo::Game::kInventorySlotCount, {}),
          "SDL-free inventory highlights the selected slot and rejects an invalid selection");

    Backend backend;
    echo::platform::GpuCanvas canvas{backend};
    backend.failMaterial = true;
    check(!canvas.initialize() && backend.textures == 0 && backend.materials == 0,
          "failed material creation releases the initial texture");
    backend.failMaterial = false;
    backend.failMesh = true;
    check(!canvas.initialize() && backend.textures == 0 && backend.materials == 0 &&
              backend.meshes == 0,
          "failed mesh creation releases the initial material and texture");
    backend.failMesh = false;
    check(static_cast<bool>(canvas.initialize()), "canvas can initialize after a failed attempt");
    backend.failTextureCall = backend.textureCalls + 3;
    check(!canvas.loadSprites(ECHO_SOURCE_ASSETS) && backend.textures == 1,
          "failed sprite upload releases all earlier sprite textures");
    backend.failTextureCall = 0;
    check(static_cast<bool>(canvas.loadSprites(ECHO_SOURCE_ASSETS)),
          "sprite upload can be retried after rollback");
    check(static_cast<bool>(canvas.shutdown()) && backend.textures == 0 &&
              backend.materials == 0 && backend.meshes == 0,
          "shutdown releases resources after a successful retry");

    TextureHandle existing{42, 1};
    std::uint32_t width = 8;
    std::uint32_t height = 8;
    check(!echo::platform::uploadCanvasTexture(backend, ECHO_SOURCE_ASSETS "sprites/player.png",
                                                existing, width, height) &&
              existing == TextureHandle{42, 1} && width == 8 && height == 8,
          "upload rejects an occupied destination without losing its handle");
    return failures == 0 ? 0 : 1;
}
