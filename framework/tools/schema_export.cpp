#include "echo/framework/capability_manifest.hpp"
#ifdef ECHO_MANIFEST_HAS_INVENTORY
#include "echo/framework/inventory.hpp"
#endif
#ifdef ECHO_MANIFEST_HAS_ATTRIBUTES
#include "echo/framework/attributes.hpp"
#endif
#ifdef ECHO_MANIFEST_HAS_EQUIPMENT
#include "echo/framework/equipment.hpp"
#endif
#ifdef ECHO_MANIFEST_HAS_ABILITIES
#include "echo/framework/abilities.hpp"
#endif
#ifdef ECHO_MANIFEST_HAS_QUESTS
#include "echo/framework/quests.hpp"
#endif
#ifdef ECHO_PROJECT_COMPONENTS
#include "game/project_components.hpp"
#endif

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
#ifdef ECHO_PROJECT_COMPONENTS
    status = echo::registerProjectComponentTypes(registry);
    if (!status) {
        std::fprintf(stderr, "Project component registration failed: %.*s\n",
                     static_cast<int>(status.detail.size()), status.detail.data());
        return 1;
    }
#endif

    std::array<echo::framework::CapabilityManifestFragment, 8> fragments{{
        echo::framework::frameworkCapabilityManifestFragment(),
    }};
#ifdef ECHO_MANIFEST_HAS_NARRATIVE
    fragments[1] = echo::framework::narrativeCapabilityManifestFragment();
    std::size_t fragmentCount = 2;
#else
    std::size_t fragmentCount = 1;
#endif
#ifdef ECHO_MANIFEST_HAS_INVENTORY
    fragments[fragmentCount++] = echo::framework::inventoryCapabilityManifestFragment();
#endif
#ifdef ECHO_MANIFEST_HAS_ATTRIBUTES
    fragments[fragmentCount++] = echo::framework::attributesCapabilityManifestFragment();
#endif
#ifdef ECHO_MANIFEST_HAS_EQUIPMENT
    fragments[fragmentCount++] = echo::framework::equipmentCapabilityManifestFragment();
#endif
#ifdef ECHO_MANIFEST_HAS_ABILITIES
    fragments[fragmentCount++] = echo::framework::abilitiesCapabilityManifestFragment();
#endif
#ifdef ECHO_MANIFEST_HAS_QUESTS
    fragments[fragmentCount++] = echo::framework::questsCapabilityManifestFragment();
#endif
#ifdef ECHO_PROJECT_COMPONENTS
    fragments[fragmentCount++] = echo::projectCapabilityManifestFragment();
#endif
    std::array<char, 128 * 1024> output{};
    std::size_t written{};
    status = echo::framework::exportCapabilityManifest(
        registry, {fragments.data(), fragmentCount},
        output.data(), output.size(), written);
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
    std::printf("Exported a manifest with %zu components from %zu capability fragments to %s.\n",
                registry.size(), fragmentCount, argv[1]);
    return 0;
}
