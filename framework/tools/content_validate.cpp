#include "json_input.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: echo_content_validate <resource-manifest.json>\n");
        return 2;
    }

    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    const echo::framework::Status inputStatus =
        echo::framework::tools::readJsonFile(argv[1], document, error);
    if (!inputStatus) {
        if (inputStatus.code == echo::framework::ErrorCode::ParseError) {
            std::fprintf(stderr, "JSON parse failed at byte %zu: %.*s\n", error.offset,
                         static_cast<int>(error.message.size()), error.message.data());
        } else {
            echo::framework::tools::reportStatus(inputStatus);
        }
        return 1;
    }

    echo::framework::ContentRegistry registry{};
    const echo::framework::Status status =
        echo::framework::loadResourceManifest(document.root(), registry);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }
    std::printf("Validated %zu resources.\n", registry.size());
    return 0;
}
