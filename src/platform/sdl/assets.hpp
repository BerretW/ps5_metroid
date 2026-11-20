#pragma once

#include <SDL.h>

#include <cstddef>

namespace echo::platform {

// Largest asset file accepted by the loaders; packaged assets are far smaller.
inline constexpr Sint64 kMaxAssetBytes = 4 * 1024 * 1024;

// Writes the packaged asset directory (with trailing separator) into `out`.
// PS5 reads from /app0/assets/, other platforms from <executable dir>/assets/.
[[nodiscard]] bool assetRoot(char* out, std::size_t size) noexcept;

// Decodes a PNG file into a texture. Returns nullptr and logs on any failure.
[[nodiscard]] SDL_Texture* loadPngTexture(SDL_Renderer* renderer, const char* path) noexcept;

struct SoundBuffer {
    Sint16* samples{};
    Uint32 count{};
};

// Loads a WAV file and converts it to signed 16-bit mono at `frequency`.
// The caller releases the buffer with freeSound.
[[nodiscard]] bool loadWavMono16(const char* path, int frequency, SoundBuffer& sound) noexcept;
void freeSound(SoundBuffer& sound) noexcept;

} // namespace echo::platform
