#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "echo/framework/canvas_batch.hpp"
#include "echo/framework/vulkan_backend.hpp"
#include "platform/sdl/assets.hpp"
#include "platform/sdl/canvas_assets.hpp"
#include "platform/sdl/framework_integration.hpp"
#include "platform/sdl/gpu_canvas.hpp"
#include "platform/sdl/renderer.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>

namespace {
using namespace echo::rendering;

bool running = true;
int selection = 0;
bool inGame = false;
bool restartGame = false;
bool jumpPressed = false;
std::array<bool, 256> keys{};

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM key, LPARAM param) {
    if (message == WM_CLOSE) {
        running = false;
        return 0;
    }
    if (message == WM_KILLFOCUS) { keys.fill(false); jumpPressed = false; }
    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        const bool down = message == WM_KEYDOWN;
        if (key < keys.size()) keys[static_cast<std::size_t>(key)] = down;
        const bool initialPress = down && (param & (1LL << 30)) == 0;
        if (initialPress && key == VK_ESCAPE) {
            if (inGame) { inGame = false; keys.fill(false); jumpPressed = false; }
            else running = false;
            return 0;
        }
        if (initialPress && inGame && key == VK_SPACE) jumpPressed = true;
        if (initialPress && !inGame && key == VK_UP) selection = (selection + 4) % 5;
        if (initialPress && !inGame && key == VK_DOWN) selection = (selection + 1) % 5;
        if (initialPress && !inGame && key == VK_RETURN && selection == 0) {
            inGame = true;
            restartGame = true;
            keys.fill(false);
        }
    }
    return DefWindowProcW(window, message, key, param);
}

bool require(Status status) {
    if (status) return true;
    std::fprintf(stderr, "Vulkan menu error [%u] %.*s: %.*s\n",
                 static_cast<unsigned>(status.code), static_cast<int>(status.subject.size()),
                 status.subject.data(), static_cast<int>(status.detail.size()), status.detail.data());
    return false;
}

} // namespace

int main(int argc, char** argv) {
    unsigned frameLimit{};
    bool validation{}, motionTest{}, canvasTextureTest{}, gameSprites{}, dataWorld{};
    const char* capture{};
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            char* end{};
            const auto count = std::strtoul(argv[++i], &end, 10);
            if (!count || count > 100000 || *end) {
                std::fprintf(stderr, "Invalid frame count\n");
                return 2;
            }
            frameLimit = static_cast<unsigned>(count);
        } else if (!std::strcmp(argv[i], "--validation")) validation = true;
        else if (!std::strcmp(argv[i], "--game")) inGame = true;
        else if (!std::strcmp(argv[i], "--motion-test")) motionTest = true;
        else if (!std::strcmp(argv[i], "--canvas-texture-test")) canvasTextureTest = true;
        else if (!std::strcmp(argv[i], "--sprites")) gameSprites = true;
        else if (!std::strcmp(argv[i], "--data-world")) dataWorld = true;
        else if (!std::strcmp(argv[i], "--capture") && i + 1 < argc) capture = argv[++i];
        else { std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]); return 2; }
    }
    if (capture && !frameLimit) { std::fprintf(stderr, "Capture requires --frames\n"); return 2; }
    if (motionTest && (!inGame || frameLimit < 60)) {
        std::fprintf(stderr, "Motion test requires --game and at least 60 frames\n");
        return 2;
    }
    if (gameSprites && !inGame) {
        std::fprintf(stderr, "Sprite test requires --game\n");
        return 2;
    }
    if (dataWorld && (!inGame || !gameSprites)) {
        std::fprintf(stderr, "Data-world test requires --game --sprites\n");
        return 2;
    }

    std::array<char, 1024> path{};
    const auto length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) { std::fprintf(stderr, "Executable path unavailable\n"); return 1; }
    auto* slash = std::strrchr(path.data(), '\\');
    if (!slash) { std::fprintf(stderr, "Executable directory unavailable\n"); return 1; }
    *slash = '\0';
    std::array<char, 1200> shaders{};
    if (std::snprintf(shaders.data(), shaders.size(), "%s\\assets\\shaders", path.data()) >=
        static_cast<int>(shaders.size())) {
        std::fprintf(stderr, "Shader path too long\n");
        return 1;
    }
    std::array<char, 1200> playerSpritePath{};
    if (std::snprintf(playerSpritePath.data(), playerSpritePath.size(),
                      "%s\\assets\\player.png", path.data()) >=
        static_cast<int>(playerSpritePath.size())) {
        std::fprintf(stderr, "Player sprite path too long\n");
        return 1;
    }

    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProcedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"EchoVulkanMenu";
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls)) { std::fprintf(stderr, "RegisterClass failed: %lu\n", GetLastError()); return 1; }
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, 1280, 720};
    if (!AdjustWindowRect(&bounds, style, FALSE)) {
        std::fprintf(stderr, "AdjustWindowRect failed: %lu\n", GetLastError());
        UnregisterClassW(cls.lpszClassName, cls.hInstance);
        return 1;
    }
    HWND window = CreateWindowW(cls.lpszClassName,
                                gameSprites ? L"Echo Vulkan gameplay test - sprite art"
                                            : L"Echo Vulkan gameplay test - fallback art",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right-bounds.left, bounds.bottom-bounds.top,
        nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) {
        std::fprintf(stderr, "CreateWindow failed: %lu\n", GetLastError());
        UnregisterClassW(cls.lpszClassName, cls.hInstance);
        return 1;
    }
    ShowWindow(window, SW_SHOW);

    VulkanBackend backend;
    auto run = [&]() -> bool {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_TIMER) != 0) {
            std::fprintf(stderr, "SDL timer initialization failed: %s\n", SDL_GetError());
            return false;
        }
        if (!require(backend.initialize({window, 1280, 720, shaders.data(), validation, 640, 360}))) return false;
        echo::platform::GpuCanvas canvas{backend};
        if (!require(canvas.initialize())) return false;
        auto& geometry = canvas.batch();
        TextureHandle texture{};
        TextureHandle playerTexture{};
        std::uint32_t playerWidth{}, playerHeight{};
        if (gameSprites) {
            std::array<char, 1200> spriteRoot{};
            const int written = std::snprintf(spriteRoot.data(), spriteRoot.size(),
                                              "%s\\assets\\", path.data());
            if (written <= 0 || written >= static_cast<int>(spriteRoot.size()) ||
                !require(canvas.loadSprites(spriteRoot.data()))) {
                std::fprintf(stderr, "Game sprite upload failed\n");
                return false;
            }
        }
        if (canvasTextureTest) {
            constexpr std::array<std::byte, 4> white{std::byte{255}, std::byte{255},
                                                     std::byte{255}, std::byte{255}};
            if (!require(backend.createTexture(1, 1, white, texture))) return false;
            if (!echo::platform::uploadCanvasTexture(backend, playerSpritePath.data(), playerTexture,
                                                    playerWidth, playerHeight) ||
                playerWidth < 24 || playerHeight < 32) {
                std::fprintf(stderr, "Original player sprite sheet is unavailable or too small\n");
                return false;
            }
        }
        echo::platform::Renderer gameMenu{echo::platform::ControlScheme::Keyboard};
        echo::platform::FrameworkIntegration gameIntegration{};
        echo::Game game;
        if (dataWorld) {
            std::array<char, 1200> assetRoot{}, configPath{}, dialoguePath{}, itemPath{};
            const auto makePath = [&](char* out, std::size_t size, const char* suffix) {
                const int written = std::snprintf(out, size, "%s\\assets\\%s",
                                                  path.data(), suffix);
                return written > 0 && written < static_cast<int>(size);
            };
            if (!makePath(assetRoot.data(), assetRoot.size(), "") ||
                !makePath(configPath.data(), configPath.size(), "framework\\game.json") ||
                !makePath(dialoguePath.data(), dialoguePath.size(), "framework\\engineer.json") ||
                !makePath(itemPath.data(), itemPath.size(), "framework\\items.json")) {
                std::fprintf(stderr, "Packaged game content path is too long\n");
                return false;
            }
            const echo::framework::Status initialized = gameIntegration.initialize(
                configPath.data(), dialoguePath.data(), itemPath.data(), assetRoot.data());
            if (!initialized) {
                std::fprintf(stderr, "Framework initialization failed [%u] %.*s: %.*s\n",
                    static_cast<unsigned>(initialized.code),
                    static_cast<int>(initialized.subject.size()), initialized.subject.data(),
                    static_cast<int>(initialized.detail.size()), initialized.detail.data());
                return false;
            }
            const echo::framework::Status configured = gameIntegration.configureGame(game);
            const echo::framework::Status begun = configured
                ? gameIntegration.beginGame() : configured;
            if (!configured || !begun ||
                !require(canvas.loadWorldSprites(assetRoot.data(), game.world()))) {
                std::fprintf(stderr, "JSON world configuration or sprite upload failed\n");
                return false;
            }
            std::printf("Loaded %zu JSON world sprite sheets\n", canvas.worldSprites().count);
        }
        const float initialX = game.player().position.x;
        auto lastTick = std::chrono::steady_clock::now();
        float accumulator{};
        unsigned frames{};
        while (running && (!frameLimit || frames < frameLimit)) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running) break;
            const auto now = std::chrono::steady_clock::now();
            if (restartGame) { game.start(); restartGame = false; accumulator = 0.0F; }
            if (inGame) {
                accumulator += frameLimit ? echo::kFixedStep :
                    std::min(0.25F, std::chrono::duration<float>(now - lastTick).count());
                while (accumulator >= echo::kFixedStep) {
                    echo::Input input{};
                    input.moveX = motionTest ? 1 : static_cast<int>(keys[VK_RIGHT]) - static_cast<int>(keys[VK_LEFT]);
                    input.moveY = static_cast<int>(keys[VK_DOWN]) - static_cast<int>(keys[VK_UP]);
                    input.jumpPressed = jumpPressed;
                    input.jumpHeld = keys[VK_SPACE];
                    input.shootHeld = keys['J'];
                    if (!require(game.update(input, echo::kFixedStep))) return false;
                    jumpPressed = false;
                    accumulator -= echo::kFixedStep;
                }
                if (!gameMenu.recordGame(geometry, game, false, false, "SPACE", "J", "E",
                                         gameSprites ? &canvas.sprites() : nullptr,
                                         dataWorld ? &canvas.worldSprites() : nullptr)) {
                    std::fprintf(stderr, "Gameplay recording failed\n");
                    return false;
                }
            } else {
                accumulator = 0.0F;
                if (!gameMenu.recordMenu(geometry, selection, false)) {
                    std::fprintf(stderr, "Game menu recording failed\n");
                    return false;
                }
            }
            if (canvasTextureTest &&
                !geometry.texture(texture, {20, 20, 80, 80}, {0, 0, 1, 1},
                                  {1, 0, 1, 1}, 45.0F)) {
                std::fprintf(stderr, "Vulkan canvas texture draw failed\n");
                return false;
            }
            if (canvasTextureTest &&
                !geometry.texture(texture, {120, 70, 80, 20}, {0, 0, 1, 1},
                                   {0, 1, 1, 1}, 90.0F)) {
                std::fprintf(stderr, "Vulkan canvas rotation test failed\n");
                return false;
            }
            if (canvasTextureTest) {
                Vec4 playerUv{};
                if (!require(spriteUvTransform(playerWidth, playerHeight,
                                               {0, 0, 24, 32}, playerUv)) ||
                    !geometry.texture(playerTexture, {500, 20, 48, 64}, playerUv,
                                       {1, 0, 1, 1}, 0.0F)) {
                    std::fprintf(stderr, "Original player atlas draw failed\n");
                    return false;
                }
            }
            lastTick = now;
            auto status = canvas.beginFrame();
            if (!status && status.code == ErrorCode::InvalidState &&
                status.detail == "swapchain out of date; retry frame") continue;
            if (!require(status)) return false;
            if (capture && frames + 1 == frameLimit && !require(backend.captureNextFrame(capture))) return false;
            if (!require(canvas.endFrame())) return false;
            ++frames;
        }
        if (frameLimit && frames != frameLimit) { std::fprintf(stderr, "Bounded run interrupted\n"); return false; }
        if (motionTest && game.player().position.x <= initialX + 80.0F) {
            std::fprintf(stderr, "Gameplay motion test did not advance the player\n");
            return false;
        }
        const auto stats = backend.diagnostics();
        std::printf("{\"stage\":\"menu.complete\",\"frames\":%llu,\"drawCalls\":%u,\"triangles\":%u,\"validationErrors\":%u}\n",
                    static_cast<unsigned long long>(stats.frames), stats.drawCalls,
                    stats.triangles, stats.validationErrors);
        if (stats.validationErrors || !require(canvas.shutdown())) return false;
        if (texture.valid() && !require(backend.destroyTexture(texture))) return false;
        if (playerTexture.valid() && !require(backend.destroyTexture(playerTexture))) return false;
        return true;
    };
    bool success = run();
    backend.shutdown();
    SDL_Quit();
    if (backend.diagnostics().validationErrors || backend.diagnostics().allocatedGpuBytes != 0) success = false;
    if (!DestroyWindow(window)) { std::fprintf(stderr, "DestroyWindow failed: %lu\n", GetLastError()); success = false; }
    if (!UnregisterClassW(cls.lpszClassName, cls.hInstance)) {
        std::fprintf(stderr, "UnregisterClass failed: %lu\n", GetLastError());
        success = false;
    }
    return success ? 0 : 1;
}
