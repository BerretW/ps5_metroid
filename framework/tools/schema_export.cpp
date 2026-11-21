#include "echo/framework/schema.hpp"

#include <array>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: echo_schema_export <output.json>\n");
        return 2;
    }

    echo::framework::ComponentTypeRegistry registry{};
    echo::framework::Status status = echo::framework::registerCoreComponentTypes(registry);
    if (!status) {
        std::fprintf(stderr, "Component registration failed: %.*s\n",
                     static_cast<int>(status.detail.size()), status.detail.data());
        return 1;
    }

    std::array<char, 64 * 1024> output{};
    std::size_t written{};
    status = echo::framework::exportComponentManifest(
        registry, output.data(), output.size(), written);
    if (!status) {
        std::fprintf(stderr, "Schema export failed: %.*s\n",
                     static_cast<int>(status.detail.size()), status.detail.data());
        return 1;
    }

    std::FILE* file = std::fopen(argv[1], "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "Could not open schema output path.\n");
        return 1;
    }
    const std::size_t writtenBytes = std::fwrite(output.data(), 1, written, file);
    const int closeResult = std::fclose(file);
    if (writtenBytes != written || closeResult != 0) {
        std::fprintf(stderr, "Could not write complete schema manifest.\n");
        return 1;
    }
    std::printf("Exported %zu component schemas to %s.\n", registry.size(), argv[1]);
    return 0;
}
