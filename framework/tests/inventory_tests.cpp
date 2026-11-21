#include "echo/framework/inventory.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <string_view>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

echo::framework::JsonDocument parse(std::string_view text) {
    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    const echo::framework::Status status =
        echo::framework::JsonDocument::parse(text.data(), text.size(), document, error);
    check(static_cast<bool>(status), "item catalog fixture parses");
    return document;
}

} // namespace

int main() {
    using namespace echo::framework;
    constexpr std::string_view validCatalog = R"({
        "schemaVersion": 1,
        "items": [
            {"id":"echo:item/maintenance_key","key":1,
             "displayName":"Maintenance Key","maxStack":1},
            {"id":"echo:item/health_cell","key":2,
             "displayName":"Health Cell","maxStack":3}
        ]
    })";
    JsonDocument document = parse(validCatalog);
    ItemCatalog catalog{};
    check(catalog.load(document.root()) && catalog.size() == 2,
          "bounded item catalog loads its stable definitions");
    const ItemDefinition* key = catalog.find("echo:item/maintenance_key");
    check(key != nullptr && key->key == 1 && key->maxStack == 1 &&
              std::string_view(key->displayName.data()) == "Maintenance Key",
          "item metadata is copied into catalog-owned storage");

    FixedInventory<std::uint32_t, 3> inventory{};
    check(inventory.add(2, 3, 5) && inventory.size() == 2 &&
              inventory.quantity(2) == 5 && inventory.valid(),
          "inventory splits a grant across bounded stacks");
    const auto beforeItems = inventory.items;
    const auto beforeQuantities = inventory.quantities;
    const std::size_t beforeSize = inventory.size();
    check(inventory.add(2, 3, 5).code == ErrorCode::CapacityExceeded &&
              inventory.items == beforeItems &&
              inventory.quantities == beforeQuantities &&
              inventory.size() == beforeSize,
          "an oversized grant fails atomically");
    check(inventory.remove(2, 4) && inventory.quantity(2) == 1 &&
              inventory.size() == 1 && inventory.valid(),
          "removing a quantity compacts empty stacks deterministically");
    check(inventory.remove(2, 2).code == ErrorCode::NotFound &&
              inventory.quantity(2) == 1,
          "removing more than owned leaves inventory unchanged");
    inventory.clear();
    check(inventory.size() == 0 && inventory.valid(),
          "inventory clear restores a valid empty state");

    for (const std::string_view invalid : {
             R"({"schemaVersion":2,"items":[{"id":"echo:item/a","key":1,"displayName":"A","maxStack":1}]})",
             R"({"schemaVersion":1,"items":[{"id":"echo:item/a","key":1,"displayName":"A","maxStack":1},{"id":"echo:item/a","key":2,"displayName":"B","maxStack":1}]})",
             R"({"schemaVersion":1,"items":[{"id":"echo:item/a","key":1,"displayName":"A","maxStack":0}]})",
             R"({"schemaVersion":1,"items":[{"id":"echo:item/a","key":1,"displayName":"A","maxStack":1,"extra":true}]})",
         }) {
        JsonDocument invalidDocument = parse(invalid);
        check(catalog.load(invalidDocument.root()).code != ErrorCode::None,
              "invalid item definitions are rejected");
        check(catalog.size() == 2,
              "failed catalog reload preserves the previously loaded catalog");
    }

    const ModuleInfo& info = InventoryModule{}.info();
    check(info.id == "echo.rpg.inventory" && info.version == 1,
          "inventory service exposes stable module metadata");
    const CapabilityManifestFragment fragment =
        inventoryCapabilityManifestFragment();
    check(fragment.modules.size() == 1 && fragment.contentSchemas.size() == 1,
          "inventory module exports its module and content schema metadata");

    if (failures != 0) {
        std::cerr << failures << " inventory test(s) failed\n";
        return 1;
    }
    std::cout << "All inventory tests passed\n";
    return 0;
}
