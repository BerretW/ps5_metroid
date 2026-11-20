/* Single translation unit for the vendored stb_image PNG decoder.
 * PS5 constraints: no stdio, no thread-local storage, SDL allocator only. */
#include <SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 4096
#define STBI_ASSERT(x) SDL_assert(x)
#define STBI_MALLOC(size) SDL_malloc(size)
#define STBI_REALLOC(pointer, size) SDL_realloc(pointer, size)
#define STBI_FREE(pointer) SDL_free(pointer)
#include "stb/stb_image.h"

unsigned char* echo_decode_png_rgba(const unsigned char* data, int length, int* width, int* height) {
    int channels = 0;
    return stbi_load_from_memory(data, length, width, height, &channels, 4);
}

void echo_free_png(unsigned char* pixels) {
    stbi_image_free(pixels);
}
