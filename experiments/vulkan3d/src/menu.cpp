#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "echo/framework/vulkan_backend.hpp"
#include "platform/sdl/renderer.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>

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

float linear(Uint8 component) noexcept {
    const float value = static_cast<float>(component) / 255.0F;
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

Vec4 color(SDL_Color value) noexcept {
    return {linear(value.r), linear(value.g), linear(value.b),
            static_cast<float>(value.a) / 255.0F};
}

class MenuGeometry final : public echo::platform::DrawSink {
public:
    bool clear(SDL_Color value) noexcept override {
        vertexCount_ = indexCount_ = 0;
        background_ = color(value);
        return true;
    }

    bool fill(SDL_Rect bounds, SDL_Color value) noexcept override {
        if (bounds.w <= 0 || bounds.h <= 0) return true;
        if (vertexCount_ > vertices_.size() - 4 || indexCount_ > indices_.size() - 6) {
            std::fprintf(stderr, "Vulkan menu geometry capacity exceeded\n");
            return false;
        }
        const float left = static_cast<float>(bounds.x) / 320.0F - 1.0F;
        const float top = static_cast<float>(bounds.y) / 180.0F - 1.0F;
        const float right = (static_cast<float>(bounds.x) + bounds.w) / 320.0F - 1.0F;
        const float bottom = (static_cast<float>(bounds.y) + bounds.h) / 180.0F - 1.0F;
        const Vec4 tint = color(value);
        const auto first = static_cast<std::uint32_t>(vertexCount_);
        vertices_[vertexCount_++] = {{left, top, 0}, {0, 0, 1}, {}, tint};
        vertices_[vertexCount_++] = {{right, top, 0}, {0, 0, 1}, {}, tint};
        vertices_[vertexCount_++] = {{right, bottom, 0}, {0, 0, 1}, {}, tint};
        vertices_[vertexCount_++] = {{left, bottom, 0}, {0, 0, 1}, {}, tint};
        for (auto index : {0U, 1U, 2U, 2U, 3U, 0U}) indices_[indexCount_++] = first + index;
        return true;
    }

    MeshView mesh() const noexcept {
        return {std::span<const Vertex>(vertices_.data(), vertexCount_),
                std::span<const std::uint32_t>(indices_.data(), indexCount_)};
    }
    Vec4 background() const noexcept { return background_; }

private:
    std::array<Vertex, kMaxMeshVertices> vertices_{};
    std::array<std::uint32_t, kMaxMeshIndices> indices_{};
    std::size_t vertexCount_{}, indexCount_{};
    Vec4 background_{};
};
} // namespace

int main(int argc, char** argv) {
    unsigned frameLimit{};
    bool validation{}, motionTest{};
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
        else if (!std::strcmp(argv[i], "--capture") && i + 1 < argc) capture = argv[++i];
        else { std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]); return 2; }
    }
    if (capture && !frameLimit) { std::fprintf(stderr, "Capture requires --frames\n"); return 2; }
    if (motionTest && (!inGame || frameLimit < 60)) {
        std::fprintf(stderr, "Motion test requires --game and at least 60 frames\n");
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
    HWND window = CreateWindowW(cls.lpszClassName, L"Echo Vulkan gameplay test - fallback art",
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
        if (!require(backend.initialize({window, 1280, 720, shaders.data(), validation}))) return false;
        auto geometry = std::unique_ptr<MenuGeometry>(new (std::nothrow) MenuGeometry);
        if (!geometry) { std::fprintf(stderr, "Menu geometry allocation failed\n"); return false; }
        constexpr std::array<std::byte, 4> white{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
        TextureHandle texture{};
        MaterialHandle material{};
        MeshHandle mesh{};
        Material info{};
        info.model = MaterialModel::VertexColor;
        if (!require(backend.createTexture(1, 1, white, texture))) return false;
        info.texture = texture;
        if (!require(backend.createMaterial(info, material)) ||
            !require(backend.createDynamicMesh(kMaxMeshVertices, kMaxMeshIndices, mesh))) return false;
        echo::platform::Renderer gameMenu{echo::platform::ControlScheme::Keyboard};
        echo::Game game;
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
                if (!gameMenu.recordGame(*geometry, game, false, false, "SPACE", "J", "E")) {
                    std::fprintf(stderr, "Fallback gameplay recording failed\n");
                    return false;
                }
            } else {
                accumulator = 0.0F;
                if (!gameMenu.recordMenu(*geometry, selection, false)) {
                    std::fprintf(stderr, "Game menu recording failed\n");
                    return false;
                }
            }
            lastTick = now;
            if (!require(backend.updateDynamicMesh(mesh, geometry->mesh()))) return false;
            FrameState frame{};
            frame.clearColor = geometry->background();
            auto status = backend.beginFrame(frame);
            if (!status && status.code == ErrorCode::InvalidState &&
                status.detail == "swapchain out of date; retry frame") continue;
            if (!require(status)) return false;
            Draw draw{mesh, material};
            draw.overlay = true;
            if (!require(backend.submit(draw))) return false;
            if (capture && frames + 1 == frameLimit && !require(backend.captureNextFrame(capture))) return false;
            if (!require(backend.endFrame())) return false;
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
        if (stats.validationErrors || !require(backend.destroyMesh(mesh)) ||
            !require(backend.destroyMaterial(material)) || !require(backend.destroyTexture(texture))) return false;
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
