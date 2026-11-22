#include "echo/framework/hierarchy.hpp"
#include "echo/framework/rendering_data.hpp"
#include "echo/framework/vfx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <string>

namespace {

int failures{};

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

bool near(float a, float b, float tolerance = 0.001F) {
    return std::abs(a - b) <= tolerance;
}

void testMathAndProjection() {
    using namespace echo::rendering;
    Mat4 view{};
    check(static_cast<bool>(lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0}, view)),
          "lookAt accepts a finite right-handed camera basis");
    const Vec4 eyeOrigin = view * Vec4{0, 0, 5, 1};
    check(near(eyeOrigin.x, 0) && near(eyeOrigin.y, 0) && near(eyeOrigin.z, 0),
          "lookAt translates its eye to origin");

    Mat4 projection{};
    check(static_cast<bool>(perspective(1.0F, 16.0F / 9.0F, 0.1F, 100.0F,
                                       projection)),
          "perspective accepts finite clip planes");
    check(projection.values[5] < 0.0F, "Vulkan positive viewport flips projection Y");
    const Vec4 nearClip = projection * Vec4{0, 0, -0.1F, 1};
    const Vec4 farClip = projection * Vec4{0, 0, -100.0F, 1};
    check(near(nearClip.z / nearClip.w, 0.0F) &&
          near(farClip.z / farClip.w, 1.0F),
          "perspective maps right-handed depth to [0,1]");
    check(!perspective(0.0F, 1.0F, 0.1F, 1.0F, projection),
          "perspective rejects invalid FOV");

    check(static_cast<bool>(orthographic(8, 4, 0, 20, projection)),
          "orthographic accepts finite bounds");
    const Vec4 orthoNear = projection * Vec4{0, 0, 0, 1};
    const Vec4 orthoFar = projection * Vec4{0, 0, -20, 1};
    check(near(orthoNear.z, 0.0F) && near(orthoFar.z, 1.0F),
          "orthographic maps right-handed depth to [0,1]");
    check(!lookAt({0, 0, 0}, {0, 0, 0}, {0, 1, 0}, view),
          "lookAt rejects a zero direction");
}

void testMeshMaterialAndCapabilities() {
    using namespace echo::rendering;
    const std::array<Vertex, 3> vertices{{
        {{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}
    }};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    check(static_cast<bool>(validateMesh({vertices, indices, {}})),
          "valid triangle mesh passes bounds checks");
    const std::array<std::uint32_t, 3> invalidIndices{0, 1, 3};
    check(!validateMesh({vertices, invalidIndices, {}}),
          "mesh rejects out-of-range indices");
    auto invalidVertices = vertices;
    invalidVertices[0].position.x = std::numeric_limits<float>::infinity();
    check(!validateMesh({invalidVertices, indices, {}}),
          "mesh rejects non-finite vertex attributes");
    const std::array<Submesh, 1> invalidSubmesh{{{2, 3}}};
    check(!validateMesh({vertices, indices, invalidSubmesh}),
          "mesh rejects out-of-range submesh spans");

    Material material{};
    check(!validateMaterial(material), "textured material requires a live texture handle");
    material.model = MaterialModel::Unlit;
    check(static_cast<bool>(validateMaterial(material)),
          "finite bounded unlit material is valid");
    material.roughness = 1.1F;
    check(!validateMaterial(material), "material rejects out-of-range PBR values");

    Capabilities caps{};
    caps.meshes = true;
    caps.depth = true;
    caps.maxMeshes = 12;
    std::array<char, 1024> json{};
    std::size_t written{};
    check(static_cast<bool>(exportCapabilities(caps, json.data(), json.size(), written)),
          "capability export succeeds in a bounded buffer");
    check(std::string_view(json.data(), written).find("\"maxMeshes\":12") !=
          std::string_view::npos, "capability JSON exports limits");
    check(!exportCapabilities(caps, json.data(), 8, written),
          "capability export rejects a short destination");
}

void testSortingAndHierarchy() {
    using namespace echo::rendering;
    Draw opaque{};
    Draw closeBlend{};
    Draw farBlend{};
    Draw overlay{};
    closeBlend.cameraDistance = 2.0F;
    farBlend.cameraDistance = 10.0F;
    overlay.overlay = true;
    check(drawBefore(opaque, AlphaMode::Opaque, closeBlend, AlphaMode::Blend),
          "opaque draws sort before transparent draws");
    check(drawBefore(farBlend, AlphaMode::Blend, closeBlend, AlphaMode::Additive),
          "transparent draws sort back-to-front");
    check(drawBefore(closeBlend, AlphaMode::Blend, overlay, AlphaMode::Opaque),
          "overlays sort after scene draws");

    EntityStore entities{};
    EntityHandle root{}, child{}, reused{};
    check(static_cast<bool>(entities.create({{2, 0, 0}, {}, {1, 1, 1}}, root)),
          "entity store creates root");
    check(static_cast<bool>(entities.create({{1, 0, 0}, {}, {1, 1, 1}}, child)),
          "entity store creates child");
    check(static_cast<bool>(entities.setParent(child, root)),
          "entity store assigns a valid parent");
    Mat4 world{};
    check(static_cast<bool>(entities.worldMatrix(child, world)) &&
          near(world.values[12], 3.0F),
          "world matrix propagates parent translation");
    check(entities.setParent(root, child).code == ErrorCode::DependencyCycle,
          "entity store rejects parent cycles");
    check(static_cast<bool>(entities.destroy(root)) && !entities.contains(root),
          "destroy invalidates entity generation");
    EntityHandle detached{};
    check(static_cast<bool>(entities.getParent(child, detached)) && !detached.valid(),
          "destroy detaches direct children");
    check(static_cast<bool>(entities.create({}, reused)) && reused.index == root.index &&
          reused.generation != root.generation,
          "reused entity slots advance their generation");
}

void testRenderingMetadataAndScene() {
    using namespace echo::framework;
    using namespace echo::rendering;
    ComponentTypeRegistry registry{};
    check(static_cast<bool>(registerCoreComponentTypes(registry)),
          "core component metadata registers");
    check(static_cast<bool>(registerRenderingComponentTypes(registry)),
          "rendering component metadata registers in existing registry");
    check(static_cast<bool>(registry.validate()), "combined component registry validates");
    std::array<char, 32768> manifest{};
    std::size_t manifestSize{};
    check(static_cast<bool>(exportComponentManifest(
              registry, manifest.data(), manifest.size(), manifestSize)),
          "component manifest exports");
    const std::string_view manifestText(manifest.data(), manifestSize);
    check(manifestText.find("echo:component/camera3d") != std::string_view::npos &&
          manifestText.find("echo:component/render_material") != std::string_view::npos,
          "manifest includes rendering component types");

    constexpr char transformJson[] =
        R"({"position":{"x":1,"y":2,"z":3},"rotation":{"x":0,"y":0,"z":0},"scale":{"x":1,"y":1,"z":1}})";
    JsonDocument document{};
    JsonError error{};
    check(static_cast<bool>(JsonDocument::parse(
              transformJson, sizeof(transformJson) - 1, document, error)),
          "transform component JSON parses");
    ComponentInstance instance{};
    check(static_cast<bool>(registry.instantiate(
              "echo:component/transform3d", document.root(), instance)),
          "transform3d schema factory instantiates");
    Transform3DComponent transform{};
    check(instance.read(transform) && near(transform.position.z, 3.0F),
          "transform component stores schema values");

    const std::array<std::string_view,3> lights{
        R"({"color":{"x":1,"y":1,"z":1},"intensity":2,"radius":3,"enabled":false})",
        R"({"color":{"x":1,"y":1,"z":1},"intensity":2,"radius":3})",
        R"({"color":{"x":1,"y":1,"z":1},"intensity":2,"radius":3,"enabled":"false"})"};
    for (std::size_t i=0;i<lights.size();++i) {
        JsonDocument lightDoc{};
        check(static_cast<bool>(JsonDocument::parse(
                  lights[i].data(),lights[i].size(),lightDoc,error)), "light JSON parses");
        const auto status=registry.instantiate("echo:component/point_light",lightDoc.root(),instance);
        PointLightComponent light{};
        if (i==2) check(!status, "light schema rejects non-boolean enabled");
        else check(static_cast<bool>(status) && instance.read(light) && light.enabled==(i==1),
                   "light schema preserves enabled/default through factory");
    }

    constexpr char materialJson[] =
        R"({"id":"echo:material/test","model":"unlit","alpha":"opaque","baseColor":[1,1,1,1],"emission":{"x":0,"y":0,"z":0},"metallic":0,"roughness":0.5})";
    JsonDocument materialDoc{};
    check(static_cast<bool>(JsonDocument::parse(
              materialJson, sizeof(materialJson) - 1, materialDoc, error)),
          "material component JSON parses");
    check(static_cast<bool>(registry.instantiate(
              "echo:component/render_material", materialDoc.root(), instance)),
          "material component factory fits bounded instance storage");
    MaterialComponent materialComponent{};
    check(instance.read(materialComponent) &&
          instance.size() <= kMaxComponentInstanceBytes,
          "material component is trivially stored below the schema size limit");
    constexpr char invalidMaterialJson[] =
        R"({"id":"not/a valid ID","model":"unlit","alpha":"opaque","baseColor":[1,1,1,1],"emission":{"x":0,"y":0,"z":0},"metallic":0,"roughness":0.5})";
    JsonDocument invalidMaterialDoc{};
    check(static_cast<bool>(JsonDocument::parse(
              invalidMaterialJson, sizeof(invalidMaterialJson) - 1,
              invalidMaterialDoc, error)),
          "invalid material ID fixture is valid JSON");
    check(!registry.instantiate("echo:component/render_material",
                                invalidMaterialDoc.root(), instance),
          "material component rejects invalid resource IDs");

    ContentRegistry resources{};
    const ResourceSpec meshes[]{
        {"echo:mesh/reactor_core", ResourceType::Generic, false, {}},
        {"echo:mesh/cargo_pod", ResourceType::Generic, false, {}},
    };
    for (const ResourceSpec& mesh : meshes) {
        check(static_cast<bool>(resources.add(mesh)), "mesh resource ID registers");
    }
    constexpr char sceneJson[] = R"({
      "schemaVersion":1,
      "id":"echo:scene/test",
      "lights":{
        "ambient":0.2,
        "directional":{"direction":{"x":0,"y":-1,"z":0},"color":{"x":1,"y":1,"z":1},"intensity":1},
        "points":[{"position":{"x":2,"y":1,"z":0},"color":{"x":0,"y":0.5,"z":1},"intensity":2,"radius":3,"enabled":false}]
      },
      "camera":{
        "position":{"x":0,"y":2,"z":6},
        "target":{"x":0,"y":0,"z":0},
        "up":{"x":0,"y":1,"z":0},
        "fovRadians":1.0,"nearPlane":0.1,"farPlane":50
      },
      "materials":[{
        "id":"echo:material/test","model":"unlit","alpha":"opaque",
        "baseColor":[1,1,1,1],"emission":{"x":0,"y":0,"z":0},
        "metallic":0,"roughness":1
      }],
      "entities":[
        {"id":"echo:entity/root","mesh":"echo:mesh/reactor_core",
         "material":"echo:material/test","position":{"x":0,"y":0,"z":0},
         "rotation":{"x":0,"y":0,"z":0},"scale":{"x":1,"y":1,"z":1}},
        {"id":"echo:entity/child","mesh":"echo:mesh/cargo_pod",
         "material":"echo:material/test","parent":"echo:entity/root",
         "position":{"x":1,"y":0,"z":0},"rotation":{"x":0,"y":0,"z":0},
         "scale":{"x":1,"y":1,"z":1}}
      ]
    })";
    SceneDocument scene{};
    check(static_cast<bool>(scene.parse(sceneJson, sizeof(sceneJson) - 1,
                                        error, &resources)),
          "data-driven scene parses and resolves mesh resource IDs");
    const SceneDefinition* definition = scene.definition();
    check(definition != nullptr && definition->entities.size() == 2 &&
          definition->entities[1].parentIndex == 0 &&
          definition->entities[1].meshId == "echo:mesh/cargo_pod",
          "scene exposes bounded material, mesh, transform, and parent data");
    check(definition && near(definition->directionalAmbient.w, 0.2F) &&
          definition->points[0].positionRadius.w == 0,
          "scene lighting loads ambient and explicitly disabled point lights");
    std::string invalidLight{sceneJson};
    const auto radiusPosition = invalidLight.find("\"radius\":3");
    invalidLight.replace(radiusPosition, 10, "\"radius\":-1");
    SceneDocument invalidLightScene{};
    check(!invalidLightScene.parse(invalidLight.data(), invalidLight.size(), error, &resources) &&
          invalidLightScene.definition() == nullptr,
          "invalid light radius is rejected without publishing scene data");
    SceneDocument missingResourceScene{};
    const Status missingMeshRegistry = missingResourceScene.parse(
        sceneJson, sizeof(sceneJson) - 1, error);
    check(missingMeshRegistry.code == ErrorCode::MissingDependency,
          "scene loader requires mesh references from the existing registry");
}

void testVfx() {
    using namespace echo::vfx;
    EmitterConfig invalid{};
    invalid.maximumParticles = static_cast<std::uint16_t>(kMaxParticlesPerEmitter + 1);
    check(!validateEmitterConfig(invalid), "VFX rejects over-capacity emitter content");
    EmitterConfig config{};
    config.maximumParticles = 4;
    config.minimumLifetime = 5.0F;
    config.maximumLifetime = 5.0F;
    config.position = {1, 2, 3};
    config.initialVelocity = {0, 0, 0};
    ParticleSystem particles{};
    EmitterHandle emitter{};
    check(static_cast<bool>(particles.createEmitter(config, emitter)),
          "VFX creates a bounded emitter");
    check(static_cast<bool>(particles.emitBurst(emitter, 1)) &&
          particles.activeParticleCount(emitter) == 1,
          "VFX emits deterministic burst particles");
    check(particles.emitBurst(emitter, 4).code ==
          echo::framework::ErrorCode::CapacityExceeded,
          "VFX rejects bursts that would exceed emitter capacity atomically");
    check(static_cast<bool>(particles.setGravity(emitter, {0, -2, 0}, 0.5F)),
          "VFX accepts a bounded gravity transition");
    check(static_cast<bool>(particles.update(0.25F)), "VFX advances one fixed step");
    auto current = particles.particles(emitter);
    check(current.size() == 1 && near(current[0].velocity.y, -0.25F),
          "VFX gravity transition is deterministic at its midpoint");
    check(current.size() == 1 && near(current[0].position.x, 1.0F) &&
          near(current[0].position.z, 3.0F) && near(current[0].scale, 0.95F) &&
          near(current[0].color.w, 0.95F),
          "VFX exposes emitter position and interpolated particle appearance");
    check(static_cast<bool>(particles.update(0.25F)), "VFX advances transition completion");
    current = particles.particles(emitter);
    check(current.size() == 1 && near(current[0].velocity.y, -0.75F),
          "VFX transition reaches target gravity");
    check(current.size() == 1 && near(current[0].scale, 0.9F) &&
          near(current[0].color.w, 0.9F),
          "VFX scale and color continue transitioning over particle lifetime");
    check(!particles.update(0.5F), "VFX rejects variable oversized simulation steps");
    const EmitterHandle stale = emitter;
    check(static_cast<bool>(particles.destroyEmitter(emitter)) &&
          !particles.destroyEmitter(stale),
          "VFX destroys resources and rejects stale generation handles");

    EmitterConfig continuous{};
    continuous.continuous = true;
    continuous.emissionRate = 4.0F;
    continuous.maximumParticles = 4;
    check(static_cast<bool>(particles.createEmitter(continuous, emitter)) &&
          static_cast<bool>(particles.update(0.25F)) &&
          particles.activeParticleCount(emitter) == 1,
          "continuous VFX emits at a deterministic fixed-step rate");

    constexpr char emitterJson[] = R"({
      "schemaVersion":1,"id":"echo:vfx/test","maximumParticles":16,
      "minimumLifetime":0.2,"maximumLifetime":0.8,"emissionRate":5,
      "position":{"x":0,"y":1,"z":0},
      "initialVelocity":{"x":0,"y":2,"z":0},
      "velocitySpread":{"x":1,"y":0.5,"z":1},
      "gravity":{"x":0,"y":-1,"z":0},
      "startColor":[1,0.5,0.2,1],"endColor":[0.1,0.2,0.8,0],
      "startScale":0.2,"endScale":0.01,"randomSeed":42,"continuous":true
    })";
    echo::vfx::EmitterDocument emitterDocument{};
    echo::framework::JsonError emitterError{};
    check(static_cast<bool>(emitterDocument.parse(
              emitterJson, sizeof(emitterJson) - 1, emitterError)),
          "bounded JsonDocument emitter loader parses valid content");
    check(emitterDocument.definition() != nullptr &&
          emitterDocument.definition()->config.continuous &&
          near(emitterDocument.definition()->config.startScale, 0.2F),
          "emitter loader exposes validated transitions and simulation settings");
    constexpr char invalidEmitterJson[] =
        R"({"schemaVersion":1,"id":"echo:vfx/bad","maximumParticles":0})";
    check(!emitterDocument.parse(invalidEmitterJson,
                                sizeof(invalidEmitterJson) - 1, emitterError) &&
          emitterDocument.definition() == nullptr,
          "emitter loader clears invalid or partial content");
    VfxModule module{};
    echo::framework::ModuleContext context{};
    check(module.info().id == "echo.vfx.cpu_particles" &&
          module.initialize(context) && module.initialized(),
          "CPU VFX exposes a static framework module");
    module.shutdown();
}

} // namespace

int main() {
    testMathAndProjection();
    testMeshMaterialAndCapabilities();
    testSortingAndHierarchy();
    testRenderingMetadataAndScene();
    testVfx();
    return failures == 0 ? 0 : 1;
}
