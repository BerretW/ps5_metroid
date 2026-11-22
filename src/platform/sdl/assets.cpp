#include "platform/sdl/assets.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
unsigned char* echo_decode_png_rgba(const unsigned char* data, int length, int* width, int* height);
int echo_png_info(const unsigned char* data, int length, int* width, int* height);
void echo_free_png(unsigned char* pixels);
}

namespace echo::platform {
namespace {

// Reads a whole file through SDL so the same path works on Windows and PS5 (/app0).
unsigned char* readFile(const char* path, int& length) noexcept {
    length = 0;
    SDL_RWops* file = SDL_RWFromFile(path, "rb");
    if (file == nullptr) {
        SDL_Log("Asset missing: %s", path);
        return nullptr;
    }
    const Sint64 size = SDL_RWsize(file);
    if (size <= 0 || size > kMaxAssetBytes) {
        SDL_Log("Asset rejected (size %lld): %s", static_cast<long long>(size), path);
        SDL_RWclose(file);
        return nullptr;
    }
    auto* data = static_cast<unsigned char*>(SDL_malloc(static_cast<std::size_t>(size)));
    std::size_t total = 0;
    while (data != nullptr && total < static_cast<std::size_t>(size)) {
        const std::size_t read = SDL_RWread(file, data + total, 1, static_cast<std::size_t>(size) - total);
        if (read == 0) {
            break;
        }
        total += read;
    }
    const int closeResult = SDL_RWclose(file);
    if (data == nullptr || total != static_cast<std::size_t>(size) || closeResult != 0) {
        SDL_Log("Asset read failed: %s", path);
        SDL_free(data);
        return nullptr;
    }
    length = static_cast<int>(size);
    return data;
}

} // namespace

bool assetRoot(char* out, std::size_t size) noexcept {
    if (out == nullptr || size == 0) {
        return false;
    }
#ifdef __PROSPERO__
    return std::snprintf(out, size, "/app0/assets/") < static_cast<int>(size);
#else
    char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return std::snprintf(out, size, "assets/") < static_cast<int>(size);
    }
    const int written = std::snprintf(out, size, "%sassets/", base);
    SDL_free(base);
    return written > 0 && written < static_cast<int>(size);
#endif
}

bool loadPngRgba(const char* path, RgbaImage& image) noexcept {
    freePngRgba(image);
    if (path == nullptr) return false;
    int length = 0;
    unsigned char* data = readFile(path, length);
    if (data == nullptr) return false;
    const bool decoded = decodePngRgba(
        {reinterpret_cast<const std::byte*>(data), static_cast<std::size_t>(length)}, image);
    SDL_free(data);
    if (!decoded) SDL_Log("PNG decode failed: %s", path);
    return decoded;
}

bool decodePngRgba(std::span<const std::byte> bytes, RgbaImage& image) noexcept {
    freePngRgba(image);
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(kMaxAssetBytes)) {
        SDL_Log("PNG input size rejected");
        return false;
    }
    int width = 0;
    int height = 0;
    if (!echo_png_info(reinterpret_cast<const unsigned char*>(bytes.data()),
                       static_cast<int>(bytes.size()), &width, &height) ||
        width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4U >
            kMaxDecodedPngBytes) {
        SDL_Log("PNG dimensions rejected");
        return false;
    }
    unsigned char* pixels = echo_decode_png_rgba(
        reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()),
        &width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4U >
            kMaxDecodedPngBytes) {
        SDL_Log("PNG decode failed or decoded size rejected");
        echo_free_png(pixels);
        return false;
    }
    image = {pixels, width, height};
    return true;
}

void freePngRgba(RgbaImage& image) noexcept {
    echo_free_png(image.pixels);
    image = {};
}

SDL_Texture* loadPngTexture(SDL_Renderer* renderer, const char* path) noexcept {
    if (renderer == nullptr || path == nullptr) {
        return nullptr;
    }
    RgbaImage image{};
    if (!loadPngRgba(path, image)) return nullptr;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(image.pixels, image.width,
                                                              image.height, 32, image.width * 4,
                                                              SDL_PIXELFORMAT_RGBA32);
    SDL_Texture* texture = surface == nullptr ? nullptr : SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
        SDL_Log("Texture creation failed for %s: %s", path, SDL_GetError());
    } else {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(surface);
    freePngRgba(image);
    return texture;
}

bool loadWavMono16(const char* path, int frequency, SoundBuffer& sound) noexcept {
    freeSound(sound);
    int length = 0;
    unsigned char* data = readFile(path, length);
    if (data == nullptr) {
        return false;
    }
    SDL_AudioSpec spec{};
    Uint8* wav = nullptr;
    Uint32 wavLength = 0;
    SDL_RWops* memory = SDL_RWFromConstMem(data, length);
    const bool loaded = memory != nullptr && SDL_LoadWAV_RW(memory, 1, &spec, &wav, &wavLength) != nullptr;
    SDL_free(data);
    if (!loaded) {
        SDL_Log("WAV decode failed for %s: %s", path, SDL_GetError());
        return false;
    }

    SDL_AudioCVT conversion{};
    const int needed = SDL_BuildAudioCVT(&conversion, spec.format, spec.channels, spec.freq,
                                         AUDIO_S16SYS, 1, frequency);
    Uint8* converted = nullptr;
    Uint32 convertedLength = 0;
    if (needed == 0) {
        converted = wav;
        convertedLength = wavLength;
        wav = nullptr;
    } else if (needed > 0) {
        conversion.len = static_cast<int>(wavLength);
        conversion.buf = static_cast<Uint8*>(SDL_malloc(static_cast<std::size_t>(wavLength) * conversion.len_mult));
        if (conversion.buf != nullptr) {
            std::memcpy(conversion.buf, wav, wavLength);
            if (SDL_ConvertAudio(&conversion) == 0) {
                converted = conversion.buf;
                convertedLength = static_cast<Uint32>(conversion.len_cvt);
            } else {
                SDL_free(conversion.buf);
            }
        }
    }
    SDL_FreeWAV(wav);
    if (converted == nullptr || convertedLength < sizeof(Sint16)) {
        SDL_Log("WAV conversion failed for %s: %s", path, SDL_GetError());
        SDL_free(converted);
        return false;
    }
    sound.samples = reinterpret_cast<Sint16*>(converted);
    sound.count = convertedLength / sizeof(Sint16);
    return true;
}

void freeSound(SoundBuffer& sound) noexcept {
    SDL_free(sound.samples);
    sound = {};
}

} // namespace echo::platform
