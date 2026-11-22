#include "echo/framework/rendering_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace echo::rendering {
namespace {

using framework::ComponentConfigView;
using framework::ComponentInstance;
using framework::ComponentTypeDescriptor;
using framework::ErrorCode;
using framework::JsonValue;
using framework::PropertyDescriptor;
using framework::PropertyType;
using framework::Status;

constexpr std::array<std::string_view, 4> kMaterialModels{
    "unlit", "vertexColor", "textured", "simplePbr"};
constexpr std::array<std::string_view, 3> kAlphaModes{
    "opaque", "blend", "additive"};

template <std::size_t Size>
[[nodiscard]] bool copyId(std::array<char, Size>& destination,
                          std::string_view id) noexcept {
    if (!framework::isValidContentId(id) || id.size() >= destination.size()) {
        return false;
    }
    std::memcpy(destination.data(), id.data(), id.size());
    destination[id.size()] = '\0';
    return true;
}

[[nodiscard]] bool parseVec3(JsonValue value, Vec3& output) noexcept {
    if (!value.isObject() || value.size() != 3) {
        return false;
    }
    double components[3]{};
    constexpr std::array<std::string_view, 3> names{"x", "y", "z"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        JsonValue component = value.find(names[index]);
        if (!component.number(components[index]) || !std::isfinite(components[index]) ||
            std::abs(components[index]) > 100000.0) {
            return false;
        }
    }
    output = {static_cast<float>(components[0]), static_cast<float>(components[1]),
              static_cast<float>(components[2])};
    return std::isfinite(output.x) && std::isfinite(output.y) &&
           std::isfinite(output.z);
}

[[nodiscard]] bool parseColor(JsonValue value, Vec4& output) noexcept {
    if (!value.isArray() || value.size() != 4) {
        return false;
    }
    double components[4]{};
    for (std::size_t index = 0; index < 4; ++index) {
        if (!value.at(index).number(components[index]) ||
            !std::isfinite(components[index]) || components[index] < 0.0 ||
            components[index] > 1.0) {
            return false;
        }
    }
    output = {static_cast<float>(components[0]), static_cast<float>(components[1]),
              static_cast<float>(components[2]), static_cast<float>(components[3])};
    return true;
}

[[nodiscard]] bool parseEnum(JsonValue value, std::span<const std::string_view> names,
                             std::size_t& output) noexcept {
    std::string_view name;
    if (!value.string(name)) {
        return false;
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index] == name) {
            output = index;
            return true;
        }
    }
    return false;
}

template <std::size_t Count>
[[nodiscard]] Status checkFields(
    JsonValue object, const std::array<std::string_view, Count>& allowed,
    std::string_view subject) noexcept {
    if (!object.isObject() || object.size() > allowed.size()) {
        return {ErrorCode::InvalidValue, subject,
                "Object shape or number of fields is invalid."};
    }
    std::array<std::string_view, Count> seen{};
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue field{};
        if (!object.memberAt(index, name, field) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return {ErrorCode::InvalidValue, subject,
                    "Object contains a field that is not supported."};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (seen[prior] == name) {
                return {ErrorCode::DuplicateId, subject,
                        "Object contains a duplicate field."};
            }
        }
        seen[index] = name;
    }
    return Status::success();
}

[[nodiscard]] bool materialModel(std::string_view value, MaterialModel& output) noexcept {
    for (std::size_t index = 0; index < kMaterialModels.size(); ++index) {
        if (kMaterialModels[index] == value) {
            output = static_cast<MaterialModel>(index);
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool alphaMode(std::string_view value, AlphaMode& output) noexcept {
    for (std::size_t index = 0; index < kAlphaModes.size(); ++index) {
        if (kAlphaModes[index] == value) {
            output = static_cast<AlphaMode>(index);
            return true;
        }
    }
    return false;
}

template <std::size_t Size>
[[nodiscard]] Status copyConfigId(std::string_view field, const ComponentConfigView& config,
                                  std::array<char, Size>& destination,
                                  bool required) noexcept {
    std::string_view value;
    const Status status = config.getString(field, value);
    if (!status) {
        if (!required && status.code == ErrorCode::NotFound) {
            destination[0] = '\0';
            return Status::success();
        }
        return status;
    }
    if (!copyId(destination, value)) {
        return {ErrorCode::InvalidValue, field,
                "Resource IDs must be stable content IDs shorter than their fixed storage."};
    }
    return Status::success();
}

[[nodiscard]] Status transformFactory(void*, const ComponentConfigView& config,
                                      ComponentInstance& output) noexcept {
    Transform3DComponent component{};
    if (!parseVec3(config.getJson("position"), component.position) ||
        !parseVec3(config.getJson("rotation"), component.rotation) ||
        !parseVec3(config.getJson("scale"), component.scale)) {
        return {ErrorCode::InvalidValue, "transform3d",
                "Transform position, rotation, and scale require finite x/y/z objects."};
    }
    return output.store(component);
}

[[nodiscard]] Status materialFactory(void*, const ComponentConfigView& config,
                                     ComponentInstance& output) noexcept {
    MaterialComponent component{};
    Status status = copyConfigId("id", config, component.id, true);
    if (!status) return status;
    status = copyConfigId("textureId", config, component.textureId, false);
    if (!status) return status;
    std::string_view modelName;
    std::string_view alphaName;
    if (!config.getString("model", modelName) ||
        !config.getString("alpha", alphaName) ||
        !materialModel(modelName, component.model) ||
        !alphaMode(alphaName, component.alpha) ||
        !parseColor(config.getJson("baseColor"), component.baseColor) ||
        !parseVec3(config.getJson("emission"), component.emission)) {
        return {ErrorCode::InvalidValue, "render_material",
                "Material model, alpha, color, and emission fields are invalid."};
    }
    double metallic{};
    double roughness{};
    if (!config.getFloat("metallic", metallic) || !config.getFloat("roughness", roughness) ||
        !std::isfinite(metallic) || !std::isfinite(roughness)) {
        return {ErrorCode::InvalidValue, "render_material",
                "Material PBR values must be finite numbers."};
    }
    component.metallic = static_cast<float>(metallic);
    component.roughness = static_cast<float>(roughness);
    return output.store(component);
}

[[nodiscard]] Status lightFactory(void*, const ComponentConfigView& config,
                                  ComponentInstance& output) noexcept {
    PointLightComponent component{};
    if (!parseVec3(config.getJson("color"), component.color)) {
        return {ErrorCode::InvalidValue, "point_light",
                "Point light color requires a finite x/y/z object."};
    }
    double intensity{};
    double radius{};
    if (!config.getFloat("intensity", intensity) || !config.getFloat("radius", radius) ||
        !std::isfinite(intensity) || !std::isfinite(radius)) {
        return {ErrorCode::InvalidValue, "point_light",
                "Point light intensity and radius must be finite numbers."};
    }
    component.intensity = static_cast<float>(intensity);
    component.radius = static_cast<float>(radius);
    if (!config.getBoolean("enabled", component.enabled))
        return {ErrorCode::InvalidValue, "point_light.enabled", "Enabled must be a boolean."};
    return output.store(component);
}

[[nodiscard]] Status cameraFactory(void*, const ComponentConfigView& config,
                                   ComponentInstance& output) noexcept {
    CameraComponent component{};
    if (!parseVec3(config.getJson("position"), component.position) ||
        !parseVec3(config.getJson("target"), component.target) ||
        !parseVec3(config.getJson("up"), component.up)) {
        return {ErrorCode::InvalidValue, "camera",
                "Camera position, target, and up require finite x/y/z objects."};
    }
    double fov{};
    double nearPlane{};
    double farPlane{};
    if (!config.getFloat("fovRadians", fov) ||
        !config.getFloat("nearPlane", nearPlane) ||
        !config.getFloat("farPlane", farPlane)) {
        return {ErrorCode::InvalidValue, "camera",
                "Camera projection fields are required."};
    }
    component.fovRadians = static_cast<float>(fov);
    component.nearPlane = static_cast<float>(nearPlane);
    component.farPlane = static_cast<float>(farPlane);
    Mat4 projection{};
    Mat4 view{};
    const Status projectionStatus = perspective(component.fovRadians, 1.0F,
        component.nearPlane, component.farPlane, projection);
    if (!projectionStatus) return projectionStatus;
    const Status viewStatus = lookAt(component.position, component.target,
                                     component.up, view);
    if (!viewStatus) return viewStatus;
    return output.store(component);
}

constexpr std::array<PropertyDescriptor, 3> kVector3Fields{{
    {.name = "x", .label = "X", .description = "Vector X component.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = -100000.0, .maximum = 100000.0},
    {.name = "y", .label = "Y", .description = "Vector Y component.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = -100000.0, .maximum = 100000.0},
    {.name = "z", .label = "Z", .description = "Vector Z component.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = -100000.0, .maximum = 100000.0},
}};
constexpr std::array<PropertyDescriptor, 3> kEmissionFields{{
    {.name = "x", .label = "X", .description = "Emitted red intensity.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 100000.0},
    {.name = "y", .label = "Y", .description = "Emitted green intensity.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 100000.0},
    {.name = "z", .label = "Z", .description = "Emitted blue intensity.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 100000.0},
}};
constexpr std::array<PropertyDescriptor, 3> kLightColorFields{{
    {.name = "x", .label = "R", .description = "Normalized red light color.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 1.0},
    {.name = "y", .label = "G", .description = "Normalized green light color.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 1.0},
    {.name = "z", .label = "B", .description = "Normalized blue light color.",
     .type = PropertyType::Float, .required = true,
     .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 1.0},
}};

constexpr std::array<PropertyDescriptor, 3> kTransformProperties{{
    {.name = "position", .label = "Position",
     .description = "Local position in right-handed world units.",
     .group = "Transform", .type = PropertyType::Object, .required = true,
     .fields = framework::PropertyDescriptorList{kVector3Fields}},
    {.name = "rotation", .label = "Rotation",
     .description = "Local XYZ Euler angles in radians.",
     .group = "Transform", .unit = "rad", .type = PropertyType::Object, .required = true,
     .fields = framework::PropertyDescriptorList{kVector3Fields}},
    {.name = "scale", .label = "Scale",
     .description = "Local scale on each axis.",
     .group = "Transform", .type = PropertyType::Object, .required = true,
     .fields = framework::PropertyDescriptorList{kVector3Fields}},
}};

constexpr std::array<PropertyDescriptor, 8> kMaterialProperties{{
    {.name = "id", .label = "Material ID", .description = "Stable material content ID.",
     .type = PropertyType::String, .required = true, .minimumLength = 1,
     .maximumLength = 63},
    {.name = "textureId", .label = "Texture ID",
     .description = "Optional stable base-color texture content ID.",
     .type = PropertyType::ResourceReference,
     .resourceType = framework::ResourceType::Generic, .maximumLength = 63},
    {.name = "model", .label = "Shading model",
     .description = "Static shading model used by the renderer.",
     .type = PropertyType::Enumeration,
     .required = true, .enumValues = kMaterialModels},
    {.name = "alpha", .label = "Alpha mode",
     .description = "Opaque, blended, or additive surface behavior.",
     .type = PropertyType::Enumeration,
     .required = true, .enumValues = kAlphaModes},
    {.name = "baseColor", .label = "Base color",
     .description = "Normalized RGBA base color.",
     .type = PropertyType::Color,
     .required = true},
    {.name = "emission", .label = "Emission",
     .description = "Non-negative emitted RGB intensity.",
     .type = PropertyType::Object,
     .required = true, .fields = framework::PropertyDescriptorList{kEmissionFields}},
    {.name = "metallic", .label = "Metallic",
     .description = "Metalness in the normalized PBR range.",
     .type = PropertyType::Float,
     .required = true, .hasMinimum = true, .hasMaximum = true,
     .minimum = 0.0, .maximum = 1.0},
    {.name = "roughness", .label = "Roughness",
     .description = "Surface roughness in the normalized PBR range.",
     .type = PropertyType::Float,
     .required = true, .hasMinimum = true, .hasMaximum = true,
     .minimum = 0.0, .maximum = 1.0},
}};
constexpr std::array<PropertyDescriptor, 4> kLightProperties{{
    {.name = "color", .label = "Color", .description = "Point light RGB color.",
     .type = PropertyType::Object,
     .required = true, .fields = framework::PropertyDescriptorList{kLightColorFields}},
    {.name = "intensity", .label = "Intensity",
     .description = "Non-negative point light intensity.",
     .type = PropertyType::Float,
     .required = true, .hasMinimum = true, .hasMaximum = true,
     .minimum = 0.0, .maximum = 100000.0},
    {.name = "radius", .label = "Radius",
     .description = "Positive attenuation radius in world units.",
     .type = PropertyType::Float,
     .required = true, .hasMinimum = true, .hasMaximum = true,
     .minimum = 0.0001, .maximum = 100000.0},
    {.name = "enabled", .label = "Enabled", .description = "Enable this light.",
     .type = PropertyType::Boolean, .hasDefault = true,
     .defaultValue = {.type = PropertyType::Boolean, .boolean = true}},
}};
constexpr std::array<PropertyDescriptor, 6> kCameraProperties{{
    {.name = "position", .label = "Position", .description = "Camera world position.",
     .type = PropertyType::Object,
     .required = true, .fields = framework::PropertyDescriptorList{kVector3Fields}},
    {.name = "target", .label = "Target", .description = "Camera look-at target.",
     .type = PropertyType::Object,
     .required = true, .fields = framework::PropertyDescriptorList{kVector3Fields}},
    {.name = "up", .label = "Up", .description = "Camera up direction.",
     .type = PropertyType::Object,
     .required = true, .fields = framework::PropertyDescriptorList{kVector3Fields}},
    {.name = "fovRadians", .label = "Vertical field of view",
     .description = "Perspective vertical field of view.", .unit = "rad",
     .type = PropertyType::Float, .required = true, .hasMinimum = true,
     .hasMaximum = true, .minimum = 0.0001, .maximum = 3.141592653589793},
    {.name = "nearPlane", .label = "Near plane",
     .description = "Positive near clipping distance.",
     .type = PropertyType::Float,
     .required = true, .hasMinimum = true, .minimum = 0.0001},
    {.name = "farPlane", .label = "Far plane",
     .description = "Far clipping distance greater than the near plane.",
     .type = PropertyType::Float,
     .required = true, .hasMinimum = true, .minimum = 0.0002},
}};

constexpr std::array<ComponentTypeDescriptor, 4> kDescriptors{{
    {"echo:component/transform3d", 1, "3D Transform",
     "Local position, Euler rotation, and scale for a right-handed 3D hierarchy.",
     "Rendering", kTransformProperties, {}, &transformFactory, nullptr, nullptr},
    {"echo:component/render_material", 1, "Render Material",
     "Stable material/texture IDs and bounded surface values.",
     "Rendering", kMaterialProperties, {}, &materialFactory, nullptr, nullptr},
    {"echo:component/point_light", 1, "Point Light",
     "Finite point-light color, intensity, and radius.",
     "Rendering", kLightProperties, {}, &lightFactory, nullptr, nullptr},
    {"echo:component/camera3d", 1, "3D Camera",
     "Right-handed camera and perspective clipping planes.",
     "Rendering", kCameraProperties, {}, &cameraFactory, nullptr, nullptr},
}};

constexpr std::array<std::string_view, 6> kSceneFields{
    "schemaVersion", "id", "camera", "materials", "entities", "lights"};
constexpr std::array<std::string_view, 6> kCameraFields{
    "position", "target", "up", "fovRadians", "nearPlane", "farPlane"};
constexpr std::array<std::string_view, 8> kMaterialFields{
    "id", "model", "alpha", "baseColor", "emission", "metallic", "roughness", "textureId"};
constexpr std::array<std::string_view, 9> kEntityFields{
    "id", "mesh", "material", "parent", "position", "rotation", "scale", "visible", "spinRadiansPerSecond"};
constexpr std::array<std::string_view, 3> kVectorFields{"x", "y", "z"};
static_assert(sizeof(MaterialComponent) <= framework::kMaxComponentInstanceBytes);

[[nodiscard]] bool requiredString(JsonValue object, std::string_view field,
                                  std::string_view& output) noexcept {
    return object.find(field).string(output) && !output.empty();
}

[[nodiscard]] bool readFinite(JsonValue object, std::string_view field,
                              float& output) noexcept {
    double value{};
    if (!object.find(field).number(value) || !std::isfinite(value) ||
        std::abs(value) > 100000.0) {
        return false;
    }
    output = static_cast<float>(value);
    return std::isfinite(output);
}

[[nodiscard]] Status parseVectorObject(JsonValue object, Vec3& output,
                                       std::string_view field) noexcept {
    const Status shape = checkFields(object, kVectorFields, field);
    if (!shape) return shape;
    if (!parseVec3(object, output)) {
        return {ErrorCode::InvalidValue, field,
                "Vector fields x, y, and z must be finite bounded numbers."};
    }
    return Status::success();
}

[[nodiscard]] std::size_t findMaterial(std::span<const SceneMaterial> materials,
                                       std::string_view id) noexcept {
    for (std::size_t index = 0; index < materials.size(); ++index) {
        if (materials[index].id == id) return index;
    }
    return materials.size();
}

} // namespace

Status registerRenderingComponentTypes(framework::ComponentTypeRegistry& registry) noexcept {
    for (const ComponentTypeDescriptor& descriptor : kDescriptors) {
        const Status status = registry.add(descriptor);
        if (!status) return status;
    }
    return Status::success();
}

Status SceneDocument::parse(const char* data, std::size_t size,
                            framework::JsonError& error,
                            const framework::ContentRegistry* resources) noexcept {
    error = {};
    reset();
    const Status parsed = framework::JsonDocument::parse(data, size, document_, error);
    if (!parsed) return parsed;
    const JsonValue root = document_.root();
    Status status = checkFields(root, kSceneFields, "scene");
    if (!status) {
        reset();
        return status;
    }
    std::int64_t version{};
    std::string_view id;
    if (!root.find("schemaVersion").integer(version) || version != 1) {
        reset();
        return {ErrorCode::UnsupportedVersion, "schemaVersion",
                "Scene JSON schemaVersion must be 1."};
    }
    if (!requiredString(root, "id", id) || !framework::isValidContentId(id)) {
        reset();
        return {ErrorCode::InvalidValue, "id", "Scene ID must be a stable content ID."};
    }
    status = parseCamera(root.find("camera"));
    if (status) status = parseMaterials(root.find("materials"));
    if (status) status = parseEntities(root.find("entities"));
    if (status) status = parseLights(root.find("lights"));
    if (status) status = validateReferences(resources);
    if (!status) {
        reset();
        return status;
    }
    definition_ = {id, camera_,
                   {materials_.data(), materialCount_},
                   {entities_.data(), entityCount_},
                   lighting_.lightDirectionAmbient, lighting_.lightColorIntensity, lighting_.pointLights};
    loaded_ = true;
    return Status::success();
}

void SceneDocument::reset() noexcept {
    document_.reset();
    materials_.fill({});
    entities_.fill({});
    materialCount_ = 0;
    entityCount_ = 0;
    camera_ = {};
    lighting_ = {};
    definition_ = {};
    loaded_ = false;
}

Status SceneDocument::parseCamera(JsonValue value) noexcept {
    const Status shape = checkFields(value, kCameraFields, "camera");
    if (!shape) return shape;
    CameraComponent camera{};
    Status status = parseVectorObject(value.find("position"), camera.position, "camera.position");
    if (status) status = parseVectorObject(value.find("target"), camera.target, "camera.target");
    if (status) status = parseVectorObject(value.find("up"), camera.up, "camera.up");
    if (!status || !readFinite(value, "fovRadians", camera.fovRadians) ||
        !readFinite(value, "nearPlane", camera.nearPlane) ||
        !readFinite(value, "farPlane", camera.farPlane)) {
        return status ? Status{ErrorCode::InvalidValue, "camera",
                               "Camera projection values must be finite numbers."}
                      : status;
    }
    Mat4 projection{};
    Mat4 view{};
    status = perspective(camera.fovRadians, 1.0F, camera.nearPlane,
                         camera.farPlane, projection);
    if (status) status = lookAt(camera.position, camera.target, camera.up, view);
    if (!status) return status;
    camera_.camera = camera;
    return Status::success();
}

Status SceneDocument::parseLights(JsonValue value) noexcept {
    if (!value.valid()) return Status::success();
    constexpr std::array<std::string_view, 3> fields{"ambient", "directional", "points"};
    Status status = checkFields(value, fields, "lights");
    if (!status) return status;
    float ambient{};
    if (!readFinite(value, "ambient", ambient) || ambient < 0 || ambient > 1)
        return {ErrorCode::InvalidValue, "lights.ambient", "Ambient must be in [0,1]."};
    auto directional = value.find("directional");
    constexpr std::array<std::string_view, 3> directionFields{"direction", "color", "intensity"};
    status = checkFields(directional, directionFields, "lights.directional");
    Vec3 direction{}, color{};
    float intensity{};
    if (status) status = parseVectorObject(directional.find("direction"), direction, "lights.direction");
    if (status) status = parseVectorObject(directional.find("color"), color, "lights.color");
    if (!status) return status;
    auto validColor = [](Vec3 c) { return c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0 && c.z <= 1; };
    if (!validColor(color) || !readFinite(directional, "intensity", intensity) || intensity < 0 ||
        dot(direction, direction) < 1e-8F)
        return {ErrorCode::InvalidValue, "lights.directional", "Invalid color, intensity or direction."};
    lighting_.lightDirectionAmbient = {direction.x, direction.y, direction.z, ambient};
    lighting_.lightColorIntensity = {color.x, color.y, color.z, intensity};
    auto points = value.find("points");
    if (!points.isArray() || points.size() > lighting_.pointLights.size())
        return {ErrorCode::CapacityExceeded, "lights.points", "At most four point lights."};
    constexpr std::array<std::string_view, 5> pointFields{"position", "color", "intensity", "radius", "enabled"};
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto point = points.at(i);
        status = checkFields(point, pointFields, "lights.point");
        Vec3 position{};
        float radius{};
        bool enabled = true;
        if (status) status = parseVectorObject(point.find("position"), position, "lights.position");
        if (status) status = parseVectorObject(point.find("color"), color, "lights.color");
        if (!status) return status;
        if (!validColor(color) || !readFinite(point, "intensity", intensity) || intensity < 0 ||
            !readFinite(point, "radius", radius) || radius <= 0 ||
            (point.find("enabled").valid() && !point.find("enabled").boolean(enabled)))
            return {ErrorCode::InvalidValue, "lights.point", "Invalid point-light parameters."};
        lighting_.pointLights[i] = {{position.x, position.y, position.z, enabled ? radius : 0},
                                  {color.x, color.y, color.z, enabled ? intensity : 0}};
    }
    return Status::success();
}

Status SceneDocument::parseMaterials(JsonValue value) noexcept {
    if (!value.isArray() || value.size() == 0 || value.size() > materials_.size()) {
        return {ErrorCode::InvalidValue, "materials",
                "Scene requires 1 to 32 material definitions."};
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const JsonValue item = value.at(index);
        const Status shape = checkFields(item, kMaterialFields, "material");
        if (!shape) return shape;
        SceneMaterial material{};
        if (!requiredString(item, "id", material.id) ||
            !framework::isValidContentId(material.id) || material.id.size() > 63) {
            return {ErrorCode::InvalidValue, "material.id",
                    "Material ID must be a stable content ID no longer than 63 bytes."};
        }
        for (std::size_t previous = 0; previous < materialCount_; ++previous) {
            if (materials_[previous].id == material.id) {
                return {ErrorCode::DuplicateId, material.id,
                        "Scene material IDs must be unique."};
            }
        }
        std::string_view name;
        if (!requiredString(item, "model", name) || !materialModel(name, material.material.model)) {
            return {ErrorCode::InvalidValue, "material.model",
                    "Material model is not supported."};
        }
        if (!requiredString(item, "alpha", name) || !alphaMode(name, material.material.alpha)) {
            return {ErrorCode::InvalidValue, "material.alpha",
                    "Material alpha mode is not supported."};
        }
        if (!parseColor(item.find("baseColor"), material.material.baseColor) ||
            !parseVectorObject(item.find("emission"), material.material.emission,
                               "material.emission") ||
            !readFinite(item, "metallic", material.material.metallic) ||
            !readFinite(item, "roughness", material.material.roughness)) {
            return {ErrorCode::InvalidValue, "material",
                    "Material colors and numeric values are invalid."};
        }
        const JsonValue texture = item.find("textureId");
        if (texture.valid() && !texture.string(material.textureId)) {
            return {ErrorCode::InvalidValue, "material.textureId",
                    "Texture ID must be a stable string reference."};
        }
        if (!material.textureId.empty() &&
            (!framework::isValidContentId(material.textureId) ||
             material.textureId.size() > 63)) {
            return {ErrorCode::InvalidValue, "material.textureId",
                    "Texture ID must be a stable content ID no longer than 63 bytes."};
        }
        Material bounded = material.material;
        if (bounded.model == MaterialModel::Textured) {
            bounded.texture = {0, 1};
        }
        const Status valid = validateMaterial(bounded);
        if (!valid) return valid;
        materials_[materialCount_++] = material;
    }
    return Status::success();
}

Status SceneDocument::parseEntities(JsonValue value) noexcept {
    if (!value.isArray() || value.size() > entities_.size()) {
        return {ErrorCode::InvalidValue, "entities",
                "Scene entities must be an array of at most 64 entries."};
    }
    std::array<std::string_view, kMaxSceneEntities> parentIds{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        const JsonValue item = value.at(index);
        const Status shape = checkFields(item, kEntityFields, "entity");
        if (!shape) return shape;
        SceneEntity entity{};
        if (!requiredString(item, "id", entity.id) ||
            !framework::isValidContentId(entity.id)) {
            return {ErrorCode::InvalidValue, "entity.id",
                    "Entity ID must be a stable content ID."};
        }
        if (!requiredString(item, "mesh", entity.meshId) ||
            !framework::isValidContentId(entity.meshId)) {
            return {ErrorCode::InvalidValue, "entity.mesh",
                    "Scene entities require a stable mesh resource ID."};
        }
        for (std::size_t previous = 0; previous < entityCount_; ++previous) {
            if (entities_[previous].id == entity.id) {
                return {ErrorCode::DuplicateId, entity.id,
                        "Scene entity IDs must be unique."};
            }
        }
        if (!requiredString(item, "material", entity.materialId)) {
            return {ErrorCode::InvalidValue, "entity.material",
                    "Scene entities require a material ID."};
        }
        Status status = parseVectorObject(item.find("position"), entity.transform.position,
                                          "entity.position");
        if (status) status = parseVectorObject(item.find("rotation"), entity.transform.rotation,
                                               "entity.rotation");
        if (status) status = parseVectorObject(item.find("scale"), entity.transform.scale,
                                               "entity.scale");
        if (!status) return status;
        if ((item.find("visible").valid() && !item.find("visible").boolean(entity.visible)) ||
            (item.find("spinRadiansPerSecond").valid() &&
             (!readFinite(item, "spinRadiansPerSecond", entity.spinRadiansPerSecond) ||
              std::abs(entity.spinRadiansPerSecond) > 100)))
            return {ErrorCode::InvalidValue, "entity.animation", "Invalid visibility or bounded spin rate."};
        const JsonValue parent = item.find("parent");
        if (parent.valid() && !parent.string(parentIds[index])) {
            return {ErrorCode::InvalidValue, "entity.parent",
                    "Parent must be a stable entity ID string."};
        }
        entities_[entityCount_++] = entity;
    }
    for (std::size_t index = 0; index < entityCount_; ++index) {
        if (parentIds[index].empty()) continue;
        std::size_t parentIndex = entityCount_;
        for (std::size_t candidate = 0; candidate < entityCount_; ++candidate) {
            if (entities_[candidate].id == parentIds[index]) {
                parentIndex = candidate;
                break;
            }
        }
        if (parentIndex == entityCount_) {
            return {ErrorCode::MissingDependency, parentIds[index],
                    "Scene entity references an unknown parent."};
        }
        entities_[index].parentIndex = static_cast<std::uint32_t>(parentIndex);
    }
    return Status::success();
}

Status SceneDocument::validateReferences(
    const framework::ContentRegistry* resources) const noexcept {
    for (const SceneMaterial& material :
         std::span<const SceneMaterial>{materials_.data(), materialCount_}) {
        if (material.textureId.empty()) continue;
        if (resources == nullptr) {
            return {ErrorCode::MissingDependency, material.textureId,
                    "Scene texture references require the existing content registry."};
        }
        framework::ResourceInfo texture{};
        const Status textureFound = resources->find(material.textureId, texture);
        if (!textureFound) {
            return {ErrorCode::MissingDependency, material.textureId,
                    "Scene material references an unregistered texture resource."};
        }
    }
    for (const SceneEntity& entity : std::span<const SceneEntity>{entities_.data(), entityCount_}) {
        if (resources == nullptr) {
            return {ErrorCode::MissingDependency, entity.meshId,
                    "Scene mesh references require the existing content registry."};
        }
        framework::ResourceInfo mesh{};
        const Status meshFound = resources->find(entity.meshId, mesh);
        if (!meshFound) {
            return {ErrorCode::MissingDependency, entity.meshId,
                    "Scene entity references an unregistered mesh resource."};
        }
        if (findMaterial({materials_.data(), materialCount_}, entity.materialId) ==
            materialCount_) {
            return {ErrorCode::MissingDependency, entity.materialId,
                    "Scene entity references an unknown material."};
        }
    }
    for (std::size_t start = 0; start < entityCount_; ++start) {
        std::array<bool, kMaxSceneEntities> visited{};
        std::uint32_t current = static_cast<std::uint32_t>(start);
        for (std::size_t depth = 0; depth <= entityCount_; ++depth) {
            if (current == UINT32_MAX) break;
            if (current >= entityCount_ || visited[current]) {
                return {ErrorCode::DependencyCycle, entities_[start].id,
                        "Scene entity parent graph contains a cycle."};
            }
            visited[current] = true;
            current = entities_[current].parentIndex;
        }
    }
    return Status::success();
}

} // namespace echo::rendering
