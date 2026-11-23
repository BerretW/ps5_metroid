#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "echo/framework/canvas_batch.hpp"
#include "echo/framework/world.hpp"
#include "echo/framework/vulkan_backend.hpp"
#include "platform/sdl/assets.hpp"
#include "platform/presentation/canvas_assets.hpp"
#include "platform/sdl/framework_integration.hpp"
#include "platform/sdl/game_persistence.hpp"
#include "platform/presentation/gpu_canvas.hpp"
#include "platform/presentation/menu_glyph.hpp"
#include "platform/presentation/ui_presentation.hpp"
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

bool recordLightingFixture(ICanvas2D& canvas,
                           const std::array<TextureHandle, 5>& textures) noexcept {
    if (!canvas.clear({0, 0, 0, 255})) return false;
    FrameState lighting{};
    lighting.lightDirectionAmbient = {-1, 0, 0, 0};
    lighting.lightColorIntensity = {1, 1, 1, 1.5F};
    lighting.ambientColor = {1, 1, 1, 1};
    const std::array<echo::framework::WorldPoint, 4> centers{{
        echo::framework::projectWorldPoint(echo::framework::RoomProjection::Side,
            {350, 180}, {260, 100}),
        echo::framework::projectWorldPoint(echo::framework::RoomProjection::Side,
            {350, 180}, {120, 100}),
        echo::framework::projectWorldPoint(echo::framework::RoomProjection::TopDown,
            {500, 150}, {180, 70}),
        echo::framework::projectWorldPoint(echo::framework::RoomProjection::Isometric,
            {600, 100}, {100, 270}),
    }};
    const std::array<Vec4, 4> colors{{
        {1, 0, 0, 1.5F}, {1, 0, 0, 1.5F},
        {0, 1, 0, 1.5F}, {0, 0, 1, 1.5F},
    }};
    for (std::size_t index = 0; index < centers.size(); ++index) {
        PointLight& light = lighting.pointLights[index];
        light.positionRadius = {centers[index].x, centers[index].y, 22.5F, 45};
        light.colorIntensity = colors[index];
        light.attenuation = {1, 0, 0, 0};
    }
    const echo::framework::WorldPoint spotDirection =
        echo::framework::projectWorldDirection(echo::framework::RoomProjection::Isometric,
                                                {1, -1});
    lighting.pointLights[3].directionCone = {
        spotDirection.x / std::hypot(spotDirection.x, spotDirection.y),
        spotDirection.y / std::hypot(spotDirection.x, spotDirection.y), 0,
        std::cos(22.5F * 0.01745329251994329577F)};
    if (!canvas.setWorldLighting(lighting) || !canvas.setWorldPass(true) ||
        !canvas.fill({0, 0, 640, 360}, {255, 255, 255, 255}) ||
        !canvas.textureWithLighting(textures[0], textures[1], {}, {},
                                    {200, 180, 60, 60}, {0, 0, 1, 1},
                                    {1, 1, 1, 1}) ||
        !canvas.textureWithLighting(textures[0], textures[2], {}, {},
                                    {280, 180, 60, 60}, {0, 0, 1, 1},
                                    {1, 1, 1, 1}) ||
        !canvas.textureWithLighting(textures[3], {}, textures[4], {0, 1.5F, 0},
                                    {380, 180, 60, 60}, {0, 0, 1, 1},
                                    {1, 1, 1, 1}) ||
        !canvas.setWorldPass(false) ||
        !canvas.fill({560, 300, 40, 30}, {255, 255, 255, 255})) {
        std::fprintf(stderr, "2D lighting or material fixture recording failed\n");
        return false;
    }
    return true;
}

bool recordIsometricLightMetricFixture(ICanvas2D& canvas) noexcept {
    if (!canvas.clear({0, 0, 0, 255})) return false;
    FrameState lighting{};
    lighting.lightDirectionAmbient = {0, 0, 0, 0};
    lighting.lightColorIntensity = {1, 1, 1, 0};
    lighting.ambientColor = {1, 1, 1, 1};
    lighting.lightScreenToWorld = {0.5F, 1.0F, -0.5F, 1.0F};
    PointLight& light = lighting.pointLights[0];
    light.positionRadius = {320, 180, 22.5F, 45};
    light.colorIntensity = {1, 1, 1, 1.5F};
    light.attenuation = {1, 0, 0, 0};
    PointLight& spot = lighting.pointLights[1];
    spot.positionRadius = {500, 180, 37.5F, 75};
    spot.colorIntensity = {0, 1, 1, 1.5F};
    spot.attenuation = {1, 0, 0, 0};
    spot.directionCone = {
        1, 0, 0, std::cos(45.0F * 0.01745329251994329577F)};
    if (!canvas.setWorldLighting(lighting) || !canvas.setWorldPass(true) ||
        !canvas.fill({0, 0, 640, 360}, {255, 255, 255, 255}) ||
        !canvas.setWorldPass(false)) {
        std::fprintf(stderr, "Isometric light metric fixture recording failed\n");
        return false;
    }
    return true;
}

bool recordShadowFixture(ICanvas2D& canvas) noexcept {
    if (!canvas.clear({0, 0, 0, 255})) return false;
    FrameState lighting{};
    lighting.lightDirectionAmbient = {0, 0, 0, 0};
    lighting.lightColorIntensity = {1, 1, 1, 0};
    lighting.ambientColor = {1, 1, 1, 0.05F};
    PointLight& light = lighting.pointLights[0];
    light.positionRadius = {180, 180, 125, 250};
    light.colorIntensity = {1, 1, 1, 2};
    light.attenuation = {1, 0, 0, 1};
    lighting.shadowInfo.x = 1;
    lighting.shadowSegments[0] = {250, 110, 250, 250};
    if (!canvas.setWorldLighting(lighting) || !canvas.setWorldPass(true) ||
        !canvas.fill({0, 0, 640, 360}, {255, 255, 255, 255}) ||
        !canvas.setWorldPass(false) ||
        !canvas.fill({560, 300, 40, 30}, {255, 255, 255, 255})) {
        std::fprintf(stderr, "2D shadow fixture recording failed\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    unsigned frameLimit{};
    bool validation{}, motionTest{}, canvasTextureTest{}, gameSprites{}, dataWorld{}, lightingTest{};
    bool isometricLightMetricTest{};
    bool shadowTest{};
    bool cargoLightingSaveTest{};
    bool platformerLightingTest{};
    const char* capture{};
    const char* glyphRoot{};
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
        else if (!std::strcmp(argv[i], "--lighting-test")) lightingTest = true;
        else if (!std::strcmp(argv[i], "--isometric-light-metric-test")) isometricLightMetricTest = true;
        else if (!std::strcmp(argv[i], "--shadow-test")) shadowTest = true;
        else if (!std::strcmp(argv[i], "--cargo-lighting-save-test")) cargoLightingSaveTest = true;
        else if (!std::strcmp(argv[i], "--platformer-lighting-test")) platformerLightingTest = true;
        else if (!std::strcmp(argv[i], "--capture") && i + 1 < argc) capture = argv[++i];
        else if (!std::strcmp(argv[i], "--glyph-root") && i + 1 < argc) glyphRoot = argv[++i];
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
    if (lightingTest && (inGame || motionTest || canvasTextureTest || gameSprites || dataWorld ||
                         glyphRoot || isometricLightMetricTest)) {
        std::fprintf(stderr, "2D lighting test cannot be combined with other scene tests\n");
        return 2;
    }
    if (isometricLightMetricTest &&
        (inGame || motionTest || canvasTextureTest || gameSprites || dataWorld ||
         lightingTest || glyphRoot || frameLimit != 1 || !capture)) {
        std::fprintf(stderr,
                     "Isometric light metric test requires --frames 1 and --capture\n");
        return 2;
    }
    if (shadowTest &&
        (inGame || motionTest || canvasTextureTest || gameSprites || dataWorld ||
         lightingTest || isometricLightMetricTest || glyphRoot || frameLimit != 1 ||
         !capture)) {
        std::fprintf(stderr, "Shadow test requires --frames 1 and --capture\n");
        return 2;
    }
    if (cargoLightingSaveTest &&
        (!inGame || !gameSprites || !dataWorld || !capture || frameLimit != 2 ||
         motionTest || canvasTextureTest || lightingTest || glyphRoot)) {
        std::fprintf(stderr,
                     "Cargo lighting save test requires --game --sprites --data-world "
                     "--frames 2 and --capture\n");
        return 2;
    }
    if (platformerLightingTest &&
        (!inGame || !gameSprites || !dataWorld || !capture || frameLimit != 4 ||
         motionTest || canvasTextureTest || lightingTest || glyphRoot ||
         cargoLightingSaveTest)) {
        std::fprintf(stderr,
                     "Platformer lighting test requires --game --sprites --data-world "
                     "--frames 4 and --capture\n");
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
                                shadowTest ? L"Echo Vulkan 2D shadow test" :
                                lightingTest ? L"Echo Vulkan 2D lighting test" :
                                isometricLightMetricTest ? L"Echo Vulkan isometric light metric test" :
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
        echo::platform::MenuGlyph glyph{backend};
        if (glyphRoot && !require(glyph.load(glyphRoot))) return false;
        auto& geometry = canvas.batch();
        std::array<TextureHandle, 5> lightingTextures{};
        TextureHandle texture{};
        TextureHandle playerTexture{};
        std::uint32_t playerWidth{}, playerHeight{};
        if (lightingTest) {
            constexpr std::array<std::byte, 4> white{
                std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
            constexpr std::array<std::byte, 4> black{
                std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
            constexpr std::array<std::byte, 4> flatNormal{
                std::byte{127}, std::byte{128}, std::byte{255}, std::byte{255}};
            constexpr std::array<std::byte, 4> xNormal{
                std::byte{255}, std::byte{128}, std::byte{128}, std::byte{255}};
            for (std::size_t index = 0; index < lightingTextures.size(); ++index) {
                const auto colorSpace = index == 1 || index == 2 || index == 4
                    ? TextureColorSpace::Linear : TextureColorSpace::Srgb;
                const auto& pixels = index == 0 || index == 4 ? white :
                                     index == 1 ? flatNormal :
                                     index == 2 ? xNormal : black;
                if (!require(backend.createTexture(1, 1, pixels,
                                                   lightingTextures[index], colorSpace)))
                    return false;
            }
        }
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
        echo::platform::Renderer restoredGameMenu{echo::platform::ControlScheme::Keyboard};
        echo::platform::Renderer* activeGameMenu = &gameMenu;
        echo::platform::FrameworkIntegration gameIntegration{};
        echo::Game game;
        std::array<char, 1200> cargoPoweredCapture{}, cargoRestoredCapture{};
        std::array<char, 1200> platformerPoweredCapture{}, platformerMovedCapture{},
            platformerExpiredCapture{}, platformerOutageCapture{};
        bool cargoLightingStateRestored{};
        bool platformerCircuitSwitched{};
        bool platformerProjectileExpired{};
        if (cargoLightingSaveTest) {
            const int poweredLength = std::snprintf(
                cargoPoweredCapture.data(), cargoPoweredCapture.size(),
                "%s.powered.ppm", capture);
            const int restoredLength = std::snprintf(
                cargoRestoredCapture.data(), cargoRestoredCapture.size(),
                "%s.restored.ppm", capture);
            if (poweredLength <= 0 ||
                poweredLength >= static_cast<int>(cargoPoweredCapture.size()) ||
                restoredLength <= 0 ||
                restoredLength >= static_cast<int>(cargoRestoredCapture.size())) {
                std::fprintf(stderr, "Cargo lighting capture path is too long\n");
                return false;
            }
        }
        if (platformerLightingTest) {
            const int poweredLength = std::snprintf(
                platformerPoweredCapture.data(), platformerPoweredCapture.size(),
                "%s.platformer-powered.ppm", capture);
            const int movedLength = std::snprintf(
                platformerMovedCapture.data(), platformerMovedCapture.size(),
                "%s.platformer-projectile.ppm", capture);
            const int expiredLength = std::snprintf(
                platformerExpiredCapture.data(), platformerExpiredCapture.size(),
                "%s.platformer-projectile-expired.ppm", capture);
            const int outageLength = std::snprintf(
                platformerOutageCapture.data(), platformerOutageCapture.size(),
                "%s.platformer-outage.ppm", capture);
            if (poweredLength <= 0 ||
                poweredLength >= static_cast<int>(platformerPoweredCapture.size()) ||
                movedLength <= 0 ||
                movedLength >= static_cast<int>(platformerMovedCapture.size()) ||
                expiredLength <= 0 ||
                expiredLength >= static_cast<int>(platformerExpiredCapture.size()) ||
                outageLength <= 0 ||
                outageLength >= static_cast<int>(platformerOutageCapture.size())) {
                std::fprintf(stderr, "Platformer lighting capture path is too long\n");
                return false;
            }
        }
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
            gameMenu.setPowerModule(gameIntegration.powerModule());
            gameMenu.setGameState(&gameIntegration.gameState());
            restoredGameMenu.setPowerModule(gameIntegration.powerModule());
            restoredGameMenu.setGameState(&gameIntegration.gameState());
            if (cargoLightingSaveTest) {
                echo::GameCheckpoint cargoCheckpoint = game.checkpoint();
                cargoCheckpoint.room = echo::RoomId::CargoHold;
                cargoCheckpoint.playerPosition = {600.0F, 170.0F};
                if (!game.restore(cargoCheckpoint)) {
                    std::fprintf(stderr, "Cargo Hold checkpoint setup failed\n");
                    return false;
                }
            }
            if (platformerLightingTest) {
                echo::GameCheckpoint platformerCheckpoint = game.checkpoint();
                platformerCheckpoint.room = echo::RoomId::LandingBay;
                platformerCheckpoint.playerPosition = {520.0F, 292.0F};
                platformerCheckpoint.playerFacing = -1;
                if (!game.restore(platformerCheckpoint) ||
                    game.nearbyPowerPanel(game.player().position) == nullptr) {
                    std::fprintf(stderr, "Landing Bay lighting scenario setup failed\n");
                    return false;
                }
            }
            std::printf("Loaded %zu JSON world sprite sheets\n", canvas.worldSprites().count);
        }
        const auto restoreCargoLightingSnapshot = [&]() noexcept {
            const auto reportFrameworkError = [](const echo::framework::Status& status,
                                                 const char* stage) {
                if (status) return true;
                std::fprintf(stderr, "%s failed [%u] %.*s: %.*s\n", stage,
                    static_cast<unsigned>(status.code),
                    static_cast<int>(status.subject.size()), status.subject.data(),
                    static_cast<int>(status.detail.size()), status.detail.data());
                return false;
            };
            const auto mainCircuit = std::string_view{"echo:power/cargo_main"};
            const auto mainLight = std::string_view{"echo:light/cargo_main"};
            if (!reportFrameworkError(gameIntegration.powerModule()->setRequested(
                    mainCircuit, false), "Disable Cargo main circuit") ||
                !reportFrameworkError(gameIntegration.setWorldLightEnabled(
                    "echo:room/cargo_hold", mainLight, false),
                    "Disable Cargo main light")) {
                return false;
            }
            auto saved = std::make_unique<echo::framework::GameState>();
            if (!reportFrameworkError(echo::platform::encodeGameCheckpoint(game, *saved),
                                      "Encode Cargo checkpoint") ||
                !reportFrameworkError(gameIntegration.powerModule()->saveState(*saved),
                                      "Save Cargo circuits") ||
                !reportFrameworkError(gameIntegration.saveWorldLightOverrides(*saved),
                                      "Save Cargo light overrides")) {
                return false;
            }
            auto serialized = std::make_unique<
                std::array<char, echo::framework::kMaxSaveBytes>>();
            std::size_t serializedSize{};
            constexpr std::array<echo::framework::ModuleVersion, 3> saveModules{{
                {"echo.state", 1, true},
                {"echo.project_echo", 1, true},
                {"echo.power", 1, false},
            }};
            if (!reportFrameworkError(echo::framework::SaveCodec::encode(
                    "echo.project_echo", 7, saveModules, *saved,
                    serialized->data(), serialized->size(), serializedSize),
                    "Encode versioned Cargo save")) {
                return false;
            }
            if (!reportFrameworkError(gameIntegration.powerModule()->setRequested(
                    mainCircuit, true), "Reset Cargo main circuit") ||
                !reportFrameworkError(gameIntegration.setWorldLightEnabled(
                    "echo:room/cargo_hold", mainLight, true),
                    "Reset Cargo main light")) {
                return false;
            }
            auto loaded = std::make_unique<echo::framework::GameState>();
            echo::framework::SaveHeader header{};
            if (!reportFrameworkError(echo::framework::SaveCodec::decode(
                    serialized->data(), serializedSize, "echo.project_echo", 7,
                    saveModules, *loaded, header,
                    &echo::platform::migrateGameCheckpointSave,
                    gameIntegration.powerModule()),
                    "Decode versioned Cargo save") ||
                header.formatVersion != 7 ||
                std::string_view(header.gameId.data()) != "echo.project_echo" ||
                header.moduleCount != saveModules.size() ||
                !reportFrameworkError(gameIntegration.resumeGame(*loaded),
                                      "Restore Cargo power and light state")) {
                return false;
            }
            echo::GameCheckpoint checkpoint{};
            if (!reportFrameworkError(echo::platform::decodeGameCheckpoint(
                    *loaded, checkpoint), "Decode Cargo checkpoint") ||
                !game.restore(checkpoint)) {
                std::fprintf(stderr, "Restore Cargo gameplay checkpoint failed\n");
                return false;
            }
            bool mainPowered{};
            bool emergencyPowered{};
            if (!reportFrameworkError(gameIntegration.powerModule()->isPowered(
                    mainCircuit, mainPowered), "Check restored Cargo main circuit") ||
                !reportFrameworkError(gameIntegration.powerModule()->isPowered(
                    "echo:power/cargo_emergency", emergencyPowered),
                    "Check restored Cargo emergency circuit")) {
                return false;
            }
            std::array<char, 64> stateKey{};
            echo::framework::StateValue lightState{};
            if (!echo::framework::worldLightStateKey(
                    "echo:room/cargo_hold", mainLight, stateKey.data(), stateKey.size()) ||
                !gameIntegration.gameState().get(
                    {echo::framework::StateScope::Global, {}, stateKey.data()}, lightState) ||
                lightState.type != echo::framework::ValueType::Boolean ||
                lightState.boolean || mainPowered || !emergencyPowered ||
                checkpoint.room != echo::RoomId::CargoHold ||
                std::abs(game.player().position.x - 600.0F) > 0.001F ||
                std::abs(game.player().position.y - 170.0F) > 0.001F) {
                std::fprintf(stderr, "Restored Cargo lighting state did not match the saved outage\n");
                return false;
            }
            return true;
        };
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
            if (cargoLightingSaveTest && frames == 1) {
                if (!restoreCargoLightingSnapshot()) return false;
                activeGameMenu = &restoredGameMenu;
                cargoLightingStateRestored = true;
            }
            const auto now = std::chrono::steady_clock::now();
            if (restartGame) { game.start(); restartGame = false; accumulator = 0.0F; }
            if (lightingTest) {
                accumulator = 0.0F;
                if (!recordLightingFixture(geometry, lightingTextures)) return false;
            } else if (isometricLightMetricTest) {
                accumulator = 0.0F;
                if (!recordIsometricLightMetricFixture(geometry)) return false;
            } else if (shadowTest) {
                accumulator = 0.0F;
                if (!recordShadowFixture(geometry)) return false;
            } else if (inGame) {
                if (cargoLightingSaveTest) {
                    accumulator = 0.0F;
                } else if (platformerLightingTest) {
                    accumulator = 0.0F;
                    if (frames == 0) {
                        echo::Input input{};
                        input.shootHeld = true;
                        if (!require(game.update(input, echo::kFixedStep))) return false;
                    } else if (frames == 1) {
                        echo::Input input{};
                        for (int step = 0; step < 10; ++step) {
                            if (!require(game.update(input, echo::kFixedStep))) return false;
                        }
                    } else if (frames == 2) {
                        echo::Input input{};
                        for (int step = 0; step < 240; ++step) {
                            if (!require(game.update(input, echo::kFixedStep))) return false;
                        }
                        platformerProjectileExpired = std::none_of(
                            game.projectiles().begin(), game.projectiles().end(),
                            [](const echo::Projectile& projectile) { return projectile.active; });
                    } else if (frames == 3) {
                        const echo::PropLayout* panel =
                            game.nearbyPowerPanel(game.player().position);
                        if (panel == nullptr ||
                            !require(gameIntegration.togglePowerCircuit(panel->circuit))) {
                            std::fprintf(stderr, "Landing Bay power-panel toggle failed\n");
                            return false;
                        }
                        bool powered{};
                        if (!require(gameIntegration.powerModule()->isPowered(
                                "echo:power/landing_main", powered)) || powered) {
                            std::fprintf(stderr, "Landing Bay main circuit stayed powered\n");
                            return false;
                        }
                        platformerCircuitSwitched = true;
                    }
                } else {
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
                }
                if (!activeGameMenu->recordGame(
                        geometry, game, false, false, "SPACE", "J", "E",
                        gameSprites ? &canvas.sprites() : nullptr,
                        dataWorld ? &canvas.worldSprites() : nullptr)) {
                    std::fprintf(stderr, "Gameplay recording failed\n");
                    return false;
                }
            } else {
                accumulator = 0.0F;
                if (!geometry.clear({7, 12, 22, 255}) ||
                    !echo::platform::drawMainMenu(geometry, echo::platform::ControlScheme::Keyboard,
                                                  selection, false, glyph.ready())) {
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
            auto status = canvas.beginFrame(glyph.ready() && !inGame ? glyph.frame() : FrameState{});
            if (!status && status.code == ErrorCode::InvalidState &&
                status.detail == "swapchain out of date; retry frame") continue;
            if (!require(status)) return false;
            if (glyph.ready() && !inGame && !require(glyph.submit(0.0F))) return false;
            if (cargoLightingSaveTest) {
                const char* frameCapture = frames == 0 ? cargoPoweredCapture.data()
                                                       : cargoRestoredCapture.data();
                if (!require(backend.captureNextFrame(frameCapture))) return false;
            } else if (platformerLightingTest) {
                const char* frameCapture = frames == 0
                    ? platformerPoweredCapture.data()
                    : frames == 1 ? platformerMovedCapture.data()
                    : frames == 2 ? platformerExpiredCapture.data()
                                  : platformerOutageCapture.data();
                if (!require(backend.captureNextFrame(frameCapture))) return false;
            } else if (capture && frames + 1 == frameLimit &&
                       !require(backend.captureNextFrame(capture))) {
                return false;
            }
            if (!require(canvas.endFrame())) return false;
            ++frames;
        }
        if (frameLimit && frames != frameLimit) { std::fprintf(stderr, "Bounded run interrupted\n"); return false; }
        if (cargoLightingSaveTest && !cargoLightingStateRestored) {
            std::fprintf(stderr, "Cargo lighting save/restore test did not reach its restored frame\n");
            return false;
        }
        if (platformerLightingTest &&
            (!platformerCircuitSwitched || !platformerProjectileExpired)) {
            std::fprintf(stderr, "Landing Bay lighting scenario did not complete\n");
            return false;
        }
        if (motionTest && game.player().position.x <= initialX + 80.0F) {
            std::fprintf(stderr, "Gameplay motion test did not advance the player\n");
            return false;
        }
        const auto stats = backend.diagnostics();
        std::printf("{\"stage\":\"menu.complete\",\"frames\":%llu,\"drawCalls\":%u,\"triangles\":%u,\"validationErrors\":%u}\n",
                    static_cast<unsigned long long>(stats.frames), stats.drawCalls,
                    stats.triangles, stats.validationErrors);
        if (cargoLightingSaveTest) {
            std::printf("{\"stage\":\"cargo_lighting.save_restore\",\"room\":\"cargo_hold\","
                        "\"main_circuit\":\"off\",\"emergency_circuit\":\"on\","
                        "\"persistent_light_override\":\"restored\"}\n");
        }
        if (platformerLightingTest) {
            std::printf("{\"stage\":\"platformer_lighting.power_switch\","
                        "\"room\":\"landing_bay\",\"main_circuit\":\"off\","
                        "\"backup_emission\":\"on\","
                        "\"projectile_light\":\"moved_then_expired\"}\n");
        }
        if (stats.validationErrors || !require(glyph.shutdown()) || !require(canvas.shutdown())) return false;
        if (texture.valid() && !require(backend.destroyTexture(texture))) return false;
        if (playerTexture.valid() && !require(backend.destroyTexture(playerTexture))) return false;
        for (TextureHandle handle : lightingTextures) {
            if (handle.valid() && !require(backend.destroyTexture(handle))) return false;
        }
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
