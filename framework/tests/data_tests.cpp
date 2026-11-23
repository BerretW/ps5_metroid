#include "echo/framework/capability_manifest.hpp"

#include <array>
#include <cstring>
#include <cstdlib>
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

void check(echo::framework::Status status, std::string_view message) {
    check(static_cast<bool>(status), message);
}

echo::framework::Status schemaTestFactory(
    void*, const echo::framework::ComponentConfigView&,
    echo::framework::ComponentInstance& output) noexcept {
    return output.store(std::uint32_t{42});
}

constexpr std::array<std::string_view, 2> kSchemaTestModes{"idle", "patrol"};
constexpr echo::framework::PropertyDescriptor kSchemaTestStringElement{
    .name = "tag", .label = "Tag", .description = "A short label.",
    .type = echo::framework::PropertyType::String,
    .maximumLength = 16,
};
constexpr std::array<echo::framework::PropertyDescriptor, 1> kSchemaTestObjectFields{{
    {.name = "opacity", .label = "Opacity", .description = "Normalized opacity.",
     .type = echo::framework::PropertyType::Float,
     .hasDefault = true,
     .defaultValue = {echo::framework::PropertyType::Float, false, 0, 1.0},
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 1.0},
}};
constexpr std::array<echo::framework::PropertyDescriptor, 6> kSchemaTestProperties{{
    {.name = "mode", .label = "Mode", .description = "Behavior mode.",
     .type = echo::framework::PropertyType::Enumeration,
     .enumValues = kSchemaTestModes},
    {.name = "sprite", .label = "Sprite", .description = "Visual resource.",
     .type = echo::framework::PropertyType::ResourceReference,
     .resourceType = echo::framework::ResourceType::Sprite, .required = true},
    {.name = "target", .label = "Target", .description = "Entity handle.",
     .type = echo::framework::PropertyType::EntityReference, .required = true},
    {.name = "tags", .label = "Tags", .description = "Bounded string labels.",
     .type = echo::framework::PropertyType::Array, .maximumLength = 4,
     .element = &kSchemaTestStringElement},
    {.name = "options", .label = "Options", .description = "Nested options.",
     .type = echo::framework::PropertyType::Object,
     .fields = kSchemaTestObjectFields},
    {.name = "color", .label = "Color", .description = "Normalized RGBA color.",
     .type = echo::framework::PropertyType::Color,
     .hasDefault = true,
     .defaultValue = {echo::framework::PropertyType::Color, false, 0, 0.0, {},
                      {1.0, 1.0, 1.0, 1.0}}},
}};
constexpr echo::framework::ComponentTypeDescriptor kSchemaTestComponent{
    "echo:component/schema_test", 1, "Schema Test", "Exercises property metadata.",
    "Testing", kSchemaTestProperties, {}, &schemaTestFactory, nullptr, nullptr};
constexpr std::array<echo::framework::ComponentDependency, 1> kSchemaTestRequiresB{{
    {"echo:component/dependency_b", true},
}};
constexpr std::array<echo::framework::ComponentDependency, 1> kSchemaTestOptionalB{{
    {"echo:component/dependency_b", false},
}};
constexpr std::array<echo::framework::ComponentDependency, 1> kSchemaTestRequiresA{{
    {"echo:component/dependency_a", true},
}};
constexpr echo::framework::ComponentTypeDescriptor kSchemaTestDependencyA{
    "echo:component/dependency_a", 1, "Dependency A", "Test dependency.",
    "Testing", {}, kSchemaTestRequiresB, &schemaTestFactory, nullptr, nullptr};
constexpr echo::framework::ComponentTypeDescriptor kSchemaTestOptionalDependencyA{
    "echo:component/dependency_a", 1, "Dependency A", "Test dependency.",
    "Testing", {}, kSchemaTestOptionalB, &schemaTestFactory, nullptr, nullptr};
constexpr echo::framework::ComponentTypeDescriptor kSchemaTestDependencyB{
    "echo:component/dependency_b", 1, "Dependency B", "Test dependency.",
    "Testing", {}, kSchemaTestRequiresA, &schemaTestFactory, nullptr, nullptr};

void testComponentSchemas() {
    using namespace echo::framework;
    ComponentTypeRegistry components{};
    check(registerCoreComponentTypes(components), "register built-in transform component");
    check(components.add(kSchemaTestComponent), "register component with nested schemas and factory");
    check(components.validate(), "component dependency graph validates");
    check(components.add(kSchemaTestComponent).code == ErrorCode::DuplicateId,
          "duplicate component IDs are rejected");

    constexpr char transformConfig[] = R"({"position":[12.5,-4],"rotationDegrees":45})";
    JsonDocument document{};
    JsonError error{};
    check(JsonDocument::parse(transformConfig, sizeof(transformConfig) - 1, document, error),
          "transform component JSON parses");
    ComponentInstance instance{};
    check(components.instantiate("echo:component/transform2d", document.root(), instance),
          "registered component factory instantiates validated configuration");
    Transform2DComponent transform{};
    check(instance.read(transform) && instance.typeId() == "echo:component/transform2d" &&
              instance.version() == 1 && transform.x == 12.5F && transform.y == -4.0F &&
              transform.scaleX == 1.0F && transform.rotationDegrees == 45.0F && transform.visible,
          "factory uses schema defaults and configured values");

    constexpr char resourcesJson[] =
        R"({"resources":[{"id":"echo:sprite/robot","type":"sprite"}]})";
    JsonDocument resourcesDocument{};
    check(JsonDocument::parse(resourcesJson, sizeof(resourcesJson) - 1,
                              resourcesDocument, error),
          "schema resource fixture parses");
    ContentRegistry resources{};
    check(loadResourceManifest(resourcesDocument.root(), resources),
          "schema resource fixture validates");

    constexpr char complexConfig[] =
        R"({"mode":"patrol","sprite":"echo:sprite/robot",)"
        R"("target":{"index":2,"generation":1},"tags":["guard","night"],)"
        R"("options":{"opacity":0.5},"color":[1,0.2,0.3,1]})";
    JsonDocument complexDocument{};
    check(JsonDocument::parse(complexConfig, sizeof(complexConfig) - 1,
                              complexDocument, error),
          "complex component JSON parses");
    check(components.validateConfig("echo:component/schema_test",
                                    complexDocument.root(), &resources),
          "enum, resource, entity, array, object and color schemas validate");
    check(components.instantiate("echo:component/schema_test", complexDocument.root(),
                                 instance, &resources),
          "custom factory is invoked after schema validation");
    std::uint32_t marker{};
    check(instance.read(marker) && marker == 42,
          "custom registered factory produces component data");

    constexpr char extraEntityField[] =
        R"({"mode":"patrol","sprite":"echo:sprite/robot",)"
        R"("target":{"index":2,"generation":1,"label":"stale"},)"
        R"("tags":["guard"]})";
    JsonDocument extraEntityDocument{};
    check(JsonDocument::parse(extraEntityField, sizeof(extraEntityField) - 1,
                              extraEntityDocument, error) &&
              components.validateConfig("echo:component/schema_test",
                                        extraEntityDocument.root(), &resources).code ==
                  ErrorCode::InvalidValue,
          "entity references reject fields beyond their stable handle");

    constexpr char excessiveArray[] =
        R"({"mode":"patrol","sprite":"echo:sprite/robot",)"
        R"("target":{"index":2,"generation":1},)"
        R"("tags":["one","two","three","four","five"]})";
    JsonDocument excessiveArrayDocument{};
    check(JsonDocument::parse(excessiveArray, sizeof(excessiveArray) - 1,
                              excessiveArrayDocument, error) &&
              components.validateConfig("echo:component/schema_test",
                                        excessiveArrayDocument.root(), &resources).code ==
                  ErrorCode::InvalidValue,
          "array item limits are enforced");

    constexpr char invalidEnum[] =
        R"({"mode":"fly","sprite":"echo:sprite/robot",)"
        R"("target":{"index":2,"generation":1}})";
    JsonDocument invalidDocument{};
    check(JsonDocument::parse(invalidEnum, sizeof(invalidEnum) - 1,
                              invalidDocument, error),
          "invalid enum fixture parses");
    check(components.validateConfig("echo:component/schema_test",
                                    invalidDocument.root(), &resources).code ==
              ErrorCode::InvalidValue,
          "unsupported enum values are rejected");

    constexpr char missingResource[] =
        R"({"mode":"idle","sprite":"echo:sprite/missing",)"
        R"("target":{"index":2,"generation":1}})";
    JsonDocument missingDocument{};
    check(JsonDocument::parse(missingResource, sizeof(missingResource) - 1,
                              missingDocument, error),
          "missing resource fixture parses");
    check(components.validateConfig("echo:component/schema_test",
                                    missingDocument.root(), &resources).code ==
              ErrorCode::MissingDependency,
          "resource references must resolve against the manifest");
    constexpr char missingRequired[] = R"({"mode":"idle"})";
    JsonDocument missingRequiredDocument{};
    check(JsonDocument::parse(missingRequired, sizeof(missingRequired) - 1,
                              missingRequiredDocument, error) &&
              components.validateConfig("echo:component/schema_test",
                                        missingRequiredDocument.root(), &resources).code ==
                  ErrorCode::InvalidValue,
          "required component properties are enforced");

    constexpr char unsupportedProperty[] = R"({"position":[0,0],"unknown":true})";
    JsonDocument unknownDocument{};
    check(JsonDocument::parse(unsupportedProperty, sizeof(unsupportedProperty) - 1,
                              unknownDocument, error) &&
              components.validateConfig("echo:component/transform2d",
                                        unknownDocument.root()).code == ErrorCode::InvalidValue,
          "unknown properties are rejected");
    constexpr char outOfRange[] = R"({"position":[100001,0]})";
    JsonDocument rangeDocument{};
    check(JsonDocument::parse(outOfRange, sizeof(outOfRange) - 1, rangeDocument, error) &&
              components.validateConfig("echo:component/transform2d",
                                        rangeDocument.root()).code == ErrorCode::InvalidValue,
          "numeric limits are enforced");

    std::array<char, 8192> manifest{};
    std::size_t manifestSize{};
    check(exportComponentManifest(components, manifest.data(), manifest.size(), manifestSize),
          "manifest exports from registered component descriptors");
    const std::string_view firstExport{manifest.data(), manifestSize};
    std::array<char, 8192> repeated{};
    std::size_t repeatedSize{};
    check(exportComponentManifest(components, repeated.data(), repeated.size(), repeatedSize) &&
              firstExport == std::string_view(repeated.data(), repeatedSize),
          "component manifest output order and bytes are deterministic");
    JsonDocument exported{};
    check(JsonDocument::parse(manifest.data(), manifestSize, exported, error),
          "exported component manifest is valid JSON");
    const JsonValue exportedComponents = exported.root().find("components");
    check(exportedComponents.isArray() && exportedComponents.size() == 2,
          "manifest includes every registered component type");
    std::array<char, 16> shortBuffer{};
    std::size_t shortSize{};
    check(exportComponentManifest(components, shortBuffer.data(), shortBuffer.size(),
                                  shortSize).code == ErrorCode::BufferTooSmall &&
              shortBuffer[0] == '\0',
          "manifest export fails explicitly when the output buffer is too small");

    check(components.validateConfig("echo:component/missing", document.root()).code ==
              ErrorCode::NotFound,
          "unregistered component types are rejected");

    ComponentTypeRegistry missingDependency{};
    check(missingDependency.add(kSchemaTestDependencyA),
          "component with a deferred dependency can be registered");
    check(missingDependency.validate().code == ErrorCode::MissingDependency,
          "missing required component dependencies are reported");

    ComponentTypeRegistry dependencyCycle{};
    check(dependencyCycle.add(kSchemaTestOptionalDependencyA) &&
              dependencyCycle.add(kSchemaTestDependencyB),
          "components with optional and required dependency edges are registered");
    check(dependencyCycle.validate().code == ErrorCode::DependencyCycle,
          "cycles through present optional dependencies are rejected");
}

void testCapabilityManifest() {
    using namespace echo::framework;
    const std::array<std::string_view, 1> required{"echo.test.base"};
    const std::array<std::string_view, 1> provided{"echo.test.service"};
    const std::array<ManifestModuleDescriptor, 2> modules{{
        {"echo.test.zeta", "Zeta", 2, required, {}, provided, {}},
        {"echo.test.alpha", "Alpha", 1, {}, {}, {}, {}},
    }};
    const std::array<ManifestCapabilityDescriptor, 1> capabilities{{
        {"echo.test.write"},
    }};
    const std::array<ManifestActionParameter, 1> parameters{{
        {"amount", "integer", true},
    }};
    const std::array<ManifestActionDescriptor, 1> actions{{
        {"echo.test.grant", "echo.test.write", false, parameters},
    }};
    const std::array<ManifestEventDescriptor, 1> events{{
        {"echo.test.event.collected", 3, parameters},
    }};
    const std::array<ManifestSchemaDescriptor, 1> schemas{{
        {"echo.test.item_json", 2},
    }};
    const std::array<std::string_view, 2> constraints{
        "Zed constraint.", "Alpha constraint."};
    const std::array<ManifestFeatureDescriptor, 1> features{{
        {"echo.test.feature", "A test feature.", constraints},
    }};
    const CapabilityManifestFragment frameworkFragment =
        frameworkCapabilityManifestFragment();
    const CapabilityManifestFragment customFragment{
        .modules = modules,
        .capabilities = capabilities,
        .actions = actions,
        .events = events,
        .contentSchemas = schemas,
        .runtimeFeatures = features,
    };
    const std::array<CapabilityManifestFragment, 2> firstOrder{{
        frameworkFragment, customFragment}};
    const std::array<CapabilityManifestFragment, 2> reverseOrder{{
        customFragment, frameworkFragment}};
    ComponentTypeRegistry components{};
    check(registerCoreComponentTypes(components),
          "capability manifest registers the authoritative built-in component schema");

    std::array<char, 32 * 1024> first{};
    std::array<char, 32 * 1024> second{};
    std::size_t firstBytes{}, secondBytes{};
    const Status firstStatus = exportCapabilityManifest(
        components, firstOrder, first.data(), first.size(), firstBytes);
    check(firstStatus, firstStatus.detail);
    if (!firstStatus) return;
    const Status secondStatus = exportCapabilityManifest(
        components, reverseOrder, second.data(), second.size(), secondBytes);
    check(secondStatus &&
              firstBytes == secondBytes &&
              std::memcmp(first.data(), second.data(), firstBytes) == 0,
          "manifest ordering is deterministic across fragment registration order");
    if (!secondStatus) return;

    JsonDocument document{};
    JsonError error{};
    const Status parseStatus = JsonDocument::parse(first.data(), firstBytes, document, error);
    check(parseStatus, parseStatus.detail);
    if (!parseStatus) return;
    const JsonValue root = document.root();
    std::int64_t version{};
    check(root.find("manifestVersion").integer(version) && version == 1 &&
              root.find("framework").find("apiVersion").integer(version) && version == 1,
          "manifest declares supported manifest and framework API versions");
    check(root.find("modules").size() == 2,
          "manifest composes module metadata and sorts by stable ID");

    std::string_view firstModule{};
    std::string_view secondModule{};
    check(root.find("modules").at(0).find("id").string(firstModule) &&
              root.find("modules").at(1).find("id").string(secondModule),
          "module identifiers are present");
    check(firstModule == "echo.test.alpha" && secondModule == "echo.test.zeta",
          "module fragments are sorted deterministically");
    check(root.find("components").size() == 1 &&
              root.find("components").at(0).find("id").string(firstModule) &&
              firstModule == "echo:component/transform2d",
          "manifest includes schemas from the live component registry");
    check(root.find("actions").size() == 1 &&
              root.find("capabilities").size() == 1 &&
              root.find("capabilities").at(0).string(firstModule) &&
              firstModule == "echo.test.write" &&
              root.find("conditionOperators").size() == 5 &&
              root.find("resourceTypes").size() == 7 &&
              root.find("events").size() == 1 &&
              root.find("contentSchemas").size() == 3 &&
              root.find("runtimeFeatures").size() == 2,
          "manifest includes action, condition, resource, event, schema, and feature metadata");

    const std::array<CapabilityManifestFragment, 2> duplicateFragments{{
        customFragment, customFragment}};
    check(exportCapabilityManifest(components, duplicateFragments, second.data(),
                                   second.size(), secondBytes).code == ErrorCode::DuplicateId,
          "duplicate IDs across module-provided fragments are rejected");
    const std::array<CapabilityManifestFragment, 1> undeclaredCapability{{
        {.actions = actions},
    }};
    check(exportCapabilityManifest(components, undeclaredCapability, second.data(),
                                   second.size(), secondBytes).code == ErrorCode::NotFound,
          "actions referencing undeclared capabilities are rejected");
    std::array<char, 1> tooSmall{'x'};
    check(exportCapabilityManifest(components, firstOrder, tooSmall.data(),
                                   tooSmall.size(), secondBytes).code ==
              ErrorCode::BufferTooSmall && tooSmall[0] == '\0',
          "manifest export reports bounded output exhaustion without partial output");
}

void testJsonAndContent() {
    using namespace echo::framework;
    constexpr char manifest[] =
        R"({"resources":[{"id":"echo:ability/echo_step","type":"ability"},)"
        R"({"id":"echo:sprite/player","type":"sprite","dependsOn":["echo:ability/echo_step"]}]})";
    JsonDocument document{};
    JsonError error{};
    check(JsonDocument::parse(manifest, sizeof(manifest) - 1, document, error),
          "bounded in-memory JSON parses");
    check(document.root().isObject(), "JSON wrapper exposes the root without yyjson types");
    ContentRegistry registry{};
    check(loadResourceManifest(document.root(), registry), "resource manifest loads and validates");
    ResourceInfo resource{};
    check(registry.find("echo:sprite/player", resource) &&
              resource.type == ResourceType::Sprite,
          "content lookup returns stable ID and resource type");
    check(registry.at(0, resource) && resource.id == "echo:ability/echo_step",
          "resource manifests preserve deterministic registration order");
    check(registry.find("echo:item/missing", resource).code == ErrorCode::NotFound,
          "missing content references are explicit");

    constexpr char sourcedDialogue[] =
        R"({"resources":[{"id":"echo:dialogue/example","type":"dialogue",)"
        R"("source":"framework/example.json"}]})";
    JsonDocument sourcedDocument{};
    check(JsonDocument::parse(sourcedDialogue, sizeof(sourcedDialogue) - 1,
                              sourcedDocument, error) &&
              loadResourceManifest(sourcedDocument.root(), registry) &&
              registry.find("echo:dialogue/example", resource) &&
              resource.source == "framework/example.json",
          "resource manifests retain a bounded relative source path");
    constexpr char escapingSource[] =
        R"({"resources":[{"id":"echo:dialogue/example","type":"dialogue",)"
        R"("source":"framework/../outside.json"}]})";
    JsonDocument escapingDocument{};
    check(JsonDocument::parse(escapingSource, sizeof(escapingSource) - 1,
                              escapingDocument, error) &&
              loadResourceManifest(escapingDocument.root(), registry).code ==
                  ErrorCode::InvalidValue,
          "resource source paths cannot escape the content root");

    constexpr char missingDependency[] =
        R"({"resources":[{"id":"echo:sprite/player","type":"sprite",)"
        R"("dependsOn":["echo:item/missing"]}]})";
    JsonDocument missingDocument{};
    check(JsonDocument::parse(missingDependency, sizeof(missingDependency) - 1,
                              missingDocument, error),
          "missing-reference fixture parses");
    check(loadResourceManifest(missingDocument.root(), registry).code ==
              ErrorCode::MissingDependency,
          "required content dependencies are validated");

    constexpr char optionalDependency[] =
        R"({"resources":[{"id":"echo:sprite/optional","type":"sprite",)"
        R"("optionalDependencies":["echo:item/not_installed"]}]})";
    JsonDocument optionalDocument{};
    check(JsonDocument::parse(optionalDependency, sizeof(optionalDependency) - 1,
                              optionalDocument, error) &&
              loadResourceManifest(optionalDocument.root(), registry),
          "missing optional content references do not block the manifest");

    constexpr char invalidJson[] = R"({"key":)";
    JsonDocument malformed{};
    const Status malformedStatus =
        JsonDocument::parse(invalidJson, sizeof(invalidJson) - 1, malformed, error);
    check(malformedStatus.code == ErrorCode::ParseError && error.offset > 0,
          "malformed JSON reports a parse offset");
    const char invalidUtf8[] = {'{', '"', 'x', '"', ':', '"', static_cast<char>(0xff), '"', '}'};
    JsonDocument invalidUtf8Document{};
    check(JsonDocument::parse(invalidUtf8, sizeof(invalidUtf8), invalidUtf8Document, error).code ==
              ErrorCode::ParseError,
          "malformed UTF-8 is rejected");
    static std::array<char, kMaxJsonBytes + 1> oversized{};
    check(JsonDocument::parse(oversized.data(), oversized.size(), malformed, error).code ==
              ErrorCode::CapacityExceeded,
          "JSON input limit is enforced before parsing");
}

void testJsonConditions() {
    using namespace echo::framework;
    constexpr char conditionJson[] =
        R"({"op":"and","conditions":[)"
        R"({"op":"exists","scope":"global","key":"story.first"},)"
        R"({"op":"compare","scope":"entity","entity":{"index":4,"generation":2},)"
        R"("key":"player.health","comparison":"gte","value":2.5},)"
        R"({"op":"not","condition":{"op":"compare","scope":"world","key":"world.level",)"
        R"("comparison":"lt","value":5}},)"
        R"({"op":"or","conditions":[)"
        R"({"op":"exists","scope":"global","key":"story.missing"},)"
        R"({"op":"compare","scope":"global","key":"story.first","comparison":"eq","value":true}]},)"
        R"({"op":"compare","scope":"world","key":"world.level","comparison":"ne","value":6},)"
        R"({"op":"compare","scope":"world","key":"world.level","comparison":"lte","value":7},)"
        R"({"op":"compare","scope":"world","key":"world.level","comparison":"gt","value":6}]})";
    JsonDocument document{};
    JsonError error{};
    check(JsonDocument::parse(conditionJson, sizeof(conditionJson) - 1, document, error),
          "condition JSON parses");
    JsonConditionTree tree{};
    check(tree.parse(document.root()), "bounded JSON condition tree decodes");
    document.reset();

    GameState state{};
    const EntityId player{4, 2};
    check(state.set({StateScope::Global, {}, "story.first"}, StateValue::fromBoolean(true)) &&
              state.set({StateScope::Entity, player, "player.health"},
                        StateValue::fromNumber(3.5)) &&
              state.set({StateScope::World, {}, "world.level"}, StateValue::fromInteger(7)),
          "condition fixture state is populated");
    const Condition* condition = tree.root();
    check(condition != nullptr && evaluate(*condition, state).status &&
              evaluate(*condition, state).value,
          "decoded nested conditions remain valid after the JSON document is released");

    constexpr char badOrdering[] =
        R"({"op":"compare","scope":"global","key":"story.first","comparison":"lt","value":"true"})";
    JsonDocument invalidDocument{};
    check(JsonDocument::parse(badOrdering, sizeof(badOrdering) - 1,
                              invalidDocument, error),
          "invalid condition fixture is valid JSON");
    check(tree.parse(invalidDocument.root()).code == ErrorCode::InvalidValue &&
              tree.root() == nullptr,
          "invalid ordering operands are rejected and clear the previous tree");

    std::array<char, 2048> tooDeep{};
    constexpr std::string_view open = R"({"op":"not","condition":)";
    constexpr std::string_view leaf =
        R"({"op":"exists","scope":"global","key":"story.first"})";
    std::size_t written = 0;
    for (std::size_t index = 0; index < 32; ++index) {
        std::memcpy(tooDeep.data() + written, open.data(), open.size());
        written += open.size();
    }
    std::memcpy(tooDeep.data() + written, leaf.data(), leaf.size());
    written += leaf.size();
    for (std::size_t index = 0; index < 32; ++index) {
        tooDeep[written++] = '}';
    }
    JsonDocument deepDocument{};
    check(JsonDocument::parse(tooDeep.data(), written, deepDocument, error) &&
              tree.parse(deepDocument.root()).code == ErrorCode::InvalidValue,
          "condition nesting beyond the evaluator limit is rejected");

    std::array<char, 8192> tooManyNodes{};
    written = 0;
    bool overflow = false;
    const auto append = [&](std::string_view text) {
        if (text.size() > tooManyNodes.size() - written) {
            overflow = true;
            return;
        }
        std::memcpy(tooManyNodes.data() + written, text.data(), text.size());
        written += text.size();
    };
    constexpr std::string_view groupStart = R"({"op":"and","conditions":[)";
    constexpr std::string_view existsLeaf =
        R"({"op":"exists","scope":"global","key":"story.first"})";
    append(groupStart);
    for (std::size_t group = 0; group < 4; ++group) {
        if (group != 0) {
            append(",");
        }
        append(groupStart);
        for (std::size_t child = 0; child < 32; ++child) {
            if (child != 0) {
                append(",");
            }
            append(existsLeaf);
        }
        append("]}");
    }
    append("]}");
    JsonDocument largeTreeDocument{};
    check(!overflow &&
              JsonDocument::parse(tooManyNodes.data(), written, largeTreeDocument, error) &&
              tree.parse(largeTreeDocument.root()).code == ErrorCode::CapacityExceeded,
          "condition trees exceeding 128 total nodes are rejected");
}

struct FakeStorage {
    std::array<char, echo::framework::kMaxSaveBytes> current{};
    std::array<char, echo::framework::kMaxSaveBytes> temporary{};
    std::array<char, 128> slots{};
    std::size_t currentSize{};
    std::size_t temporarySize{};
    std::size_t slotListSize{};
    bool failWrite{};
    bool failReplace{};
    bool failRead{};
};

bool readFake(void* raw, std::string_view slot, char* output,
              std::size_t capacity, std::size_t& size) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a" || storage.failRead || storage.currentSize > capacity) {
        return false;
    }
    std::memcpy(output, storage.current.data(), storage.currentSize);
    size = storage.currentSize;
    return true;
}

bool writeTemporaryFake(void* raw, std::string_view slot,
                        const char* data, std::size_t size) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a" || storage.failWrite || size > storage.temporary.size()) {
        return false;
    }
    std::memcpy(storage.temporary.data(), data, size);
    storage.temporarySize = size;
    return true;
}

bool replaceFake(void* raw, std::string_view slot) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a" || storage.failReplace) {
        return false;
    }
    std::memcpy(storage.current.data(), storage.temporary.data(), storage.temporarySize);
    storage.currentSize = storage.temporarySize;
    storage.temporarySize = 0;
    return true;
}

bool removeTemporaryFake(void* raw, std::string_view slot) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (slot != "slot_a") {
        return false;
    }
    storage.temporarySize = 0;
    return true;
}

bool enumerateFake(void* raw, char* output, std::size_t capacity,
                   std::size_t& written) noexcept {
    auto& storage = *static_cast<FakeStorage*>(raw);
    if (storage.slotListSize > capacity) {
        return false;
    }
    std::memcpy(output, storage.slots.data(), storage.slotListSize);
    written = storage.slotListSize;
    return true;
}

bool migrateV1(std::uint32_t fromVersion, echo::framework::GameState& state,
               void*) noexcept {
    return fromVersion == 1 &&
           static_cast<bool>(state.set({echo::framework::StateScope::Global, {},
                                        "save.migrated"},
                                       echo::framework::StateValue::fromBoolean(true)));
}

void testSaveCodecAndStorage() {
    using namespace echo::framework;
    GameState state{};
    StateValue stringValue{};
    check(StateValue::fromString("line one\n\"line two\", \"integrity\":\"nested\"", stringValue),
          "bounded UTF-8 state string is created");
    check(state.set({StateScope::Global, {}, "story.generator_repaired"},
                    StateValue::fromBoolean(true)),
          "global state is stored before saving");
    check(state.set({StateScope::World, {}, "world.level"}, StateValue::fromInteger(7)),
          "world state is stored before saving");
    check(state.set({StateScope::Entity, {4, 9}, "player.health"},
                    StateValue::fromNumber(3.5)),
          "entity-scoped numeric state is stored before saving");
    check(state.set({StateScope::Global, {}, "player.name"}, stringValue),
          "string state is stored before saving");

    constexpr std::array<ModuleVersion, 2> modules{{
        {"echo.core", 1, true},
        {"echo.dialogue", 1, false},
    }};
    std::array<char, kMaxSaveBytes> save{};
    std::size_t saveSize{};
    check(SaveCodec::encode("echo.project_echo", 1, modules, state,
                            save.data(), save.size(), saveSize),
          "versioned save serializes to bounded JSON");
    check(saveSize != 0 && saveSize < save.size(), "save writer reports its output size");

    GameState restored{};
    SaveHeader header{};
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 1,
                            modules, restored, header),
          "versioned save restores into a separate state instance");
    StateValue restoredValue{};
    check(header.formatVersion == 1 && header.moduleCount == 2 &&
              restored.get({StateScope::Global, {}, "story.generator_repaired"}, restoredValue) &&
              restoredValue.boolean,
          "save header and global state round-trip");
    check(restored.get({StateScope::World, {}, "world.level"}, restoredValue) &&
              restoredValue.integer == 7,
          "world state round-trips");
    check(restored.get({StateScope::Entity, {4, 9}, "player.health"}, restoredValue) &&
              restoredValue.number == 3.5,
          "entity-scoped state round-trips");
    check(restored.get({StateScope::Global, {}, "player.name"}, restoredValue) &&
              restoredValue.stringValue() == "line one\n\"line two\", \"integrity\":\"nested\"",
          "escaped UTF-8 string state round-trips");

    check(SaveCodec::decode(save.data(), saveSize, "echo.other_game", 1,
                            modules, restored, header).code == ErrorCode::InvalidValue,
          "saves cannot be loaded by another game identifier");
    constexpr std::array<ModuleVersion, 1> incompatible{{{"echo.core", 2, true}}};
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 1,
                            incompatible, restored, header).code == ErrorCode::UnsupportedVersion,
          "module version mismatches are rejected");
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 2,
                            modules, restored, header).code == ErrorCode::UnsupportedVersion,
          "format mismatch without migration is rejected");
    check(SaveCodec::decode(save.data(), saveSize, "echo.project_echo", 2,
                            modules, restored, header, &migrateV1),
          "registered save migration permits an older compatible schema");
    check(restored.contains({StateScope::Global, {}, "save.migrated"}),
          "migration runs on validated candidate state");
    std::array<char, kMaxSaveBytes> damaged{};
    std::memcpy(damaged.data(), save.data(), saveSize);
    damaged[10] = damaged[10] == '1' ? '2' : '1';
    check(SaveCodec::decode(damaged.data(), saveSize, "echo.project_echo", 1,
                            modules, restored, header).code == ErrorCode::CorruptData,
          "save integrity hash detects modified content");
    check(SaveCodec::decode("{", 1, "echo.project_echo", 1,
                            modules, restored, header).code == ErrorCode::CorruptData,
          "save without an integrity record is rejected");
    constexpr char missingState[] =
        R"({"format":1,"game":"echo.project_echo","modules":[],"integrity":"e3f722e4d9d0543b"})";
    const Status missingStateStatus = SaveCodec::decode(
        missingState, sizeof(missingState) - 1, "echo.project_echo", 1,
        modules, restored, header);
    check(missingStateStatus.code == ErrorCode::CorruptData &&
              missingStateStatus.detail == "Save is missing required fields or exceeds schema limits." &&
              restored.contains({StateScope::Global, {}, "save.migrated"}),
          "checksummed save missing required state data fails without changing destination state");
    check(SaveCodec::decode(save.data(), kMaxSaveBytes + 1, "echo.project_echo", 1,
                            modules, restored, header).code == ErrorCode::InvalidArgument,
          "excessively large save is rejected before parsing");

    FakeStorage fake{};
    std::memcpy(fake.current.data(), "old-valid-save", 14);
    fake.currentSize = 14;
    fake.failReplace = true;
    SaveStorage storage{&fake, &readFake, &writeTemporaryFake, &replaceFake,
                        &removeTemporaryFake, &enumerateFake};
    check(SaveService::save(storage, "slot_a", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()).code ==
              ErrorCode::StorageFailure,
          "failed replacement is reported");
    check(fake.currentSize == 14 &&
              std::string_view(fake.current.data(), fake.currentSize) == "old-valid-save" &&
              fake.temporarySize == 0,
          "failed replacement preserves the previous save and removes only the temporary");
    fake.failReplace = false;
    fake.failWrite = true;
    check(SaveService::save(storage, "slot_a", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()).code ==
              ErrorCode::StorageFailure && fake.currentSize == 14,
          "failed temporary write preserves the previous save");
    fake.failWrite = false;
    check(SaveService::save(storage, "slot_a", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()),
          "storage abstraction writes and replaces a slot");
    GameState loaded{};
    check(SaveService::load(storage, "slot_a", "echo.project_echo", 1,
                            modules, loaded, header, save.data(), save.size()),
          "storage abstraction reads and validates a slot");
    fake.failRead = true;
    check(SaveService::load(storage, "slot_a", "echo.project_echo", 1,
                            modules, loaded, header, save.data(), save.size()).code ==
              ErrorCode::StorageFailure,
          "failed storage read is reported");
    fake.failRead = false;
    check(SaveService::save(storage, "../unrelated", "echo.project_echo", 1,
                            modules, state, save.data(), save.size()).code ==
              ErrorCode::InvalidArgument,
          "unsafe slot paths are rejected");
    check(SaveService::validSlot("quick-save_1") && !SaveService::validSlot(".."),
          "slot IDs accept only a bounded safe character set");

    std::memcpy(fake.slots.data(), "slot_a\0slot-b\0", 14);
    fake.slotListSize = 14;
    std::array<char, 128> slotOutput{};
    std::size_t slotOutputSize{};
    check(SaveService::enumerate(storage, slotOutput.data(), slotOutput.size(), slotOutputSize) &&
              slotOutputSize == fake.slotListSize,
          "slot enumeration validates and returns bounded NUL-separated IDs");
}

} // namespace

int main() {
    testComponentSchemas();
    testCapabilityManifest();
    testJsonAndContent();
    testJsonConditions();
    testSaveCodecAndStorage();
    if (failures != 0) {
        std::cerr << failures << " framework data test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework data tests passed\n";
    return EXIT_SUCCESS;
}
