# Materials and data-driven scenes

Rendering component descriptors are registered into the existing
`framework::ComponentTypeRegistry` with
`registerRenderingComponentTypes(registry)`. They are exported with the
existing `exportComponentManifest`; there is no second rendering registry.
The descriptors cover `echo:component/transform3d`,
`echo:component/render_material`, `echo:component/point_light`, and
`echo:component/camera3d`.

The component schema has no `Vector3` property type. Every three-dimensional
position, rotation, scale, emission, light color, and camera vector is therefore
an `Object` with required numeric `x`, `y`, and `z` fields. `Vec3` values use
right-handed Y-up world units; rotations are XYZ Euler radians. `Color`
properties are normalized RGBA arrays.
The fixed `MaterialComponent` record is under the framework's 256-byte
component-instance limit; its material and texture IDs are each limited to 63
UTF-8 bytes plus a terminator.

`SceneDocument::parse(bytes, size, error, resources)` uses the bounded
`JsonDocument` parser and holds the parsed document alive while its
`SceneDefinition` string views are used. It caps materials at 32 and entities
at 64. Scene IDs, entity IDs, material IDs, and mesh IDs use stable
`namespace:type/name` IDs. Mesh IDs must resolve in the existing
`framework::ContentRegistry`; the scene loader does not create a parallel
resource registry or load code from JSON. Entity parent IDs are resolved to
indices and the parent graph is checked for cycles.

The sample is `experiments/vulkan3d/assets/scene.json`. Its shape is:

```json
{
  "schemaVersion": 1,
  "id": "echo:scene/example",
  "camera": {
    "position": {"x": 0, "y": 2, "z": 6},
    "target": {"x": 0, "y": 0, "z": 0},
    "up": {"x": 0, "y": 1, "z": 0},
    "fovRadians": 1.0,
    "nearPlane": 0.1,
    "farPlane": 100
  },
  "materials": [{
    "id": "echo:material/metal",
    "model": "simplePbr",
    "alpha": "opaque",
    "baseColor": [0.3, 0.4, 0.5, 1],
    "emission": {"x": 0, "y": 0, "z": 0},
    "metallic": 0.7,
    "roughness": 0.5
  }],
  "entities": [{
    "id": "echo:entity/cargo",
    "mesh": "echo:mesh/cargo_pod",
    "material": "echo:material/metal",
    "position": {"x": 0, "y": 0, "z": 0},
    "rotation": {"x": 0, "y": 0, "z": 0},
    "scale": {"x": 1, "y": 1, "z": 1}
  }]
}
```

The caller registers the referenced mesh IDs in `ContentRegistry` before
parsing. A scene material may omit `textureId`; the caller may bind the
backend's white fallback texture. When a symbolic `textureId` is supplied, the
caller resolves it and passes its live `TextureHandle` before calling
`IRenderBackend::createMaterial`. Only the base texture slot is supported by
the current Vulkan backend; normal maps and other texture channels are not
implemented. The scene loader describes data but does not allocate GPU
resources.

The Vulkan shader implements unlit, vertex-color, textured Lambert, and a small
GGX/Schlick metallic/roughness approximation. It has ambient/directional light
and at most four point lights; this is not a complete PBR/IBL renderer.
The optional scene `lights` object configures ambient, direction, directional
color/intensity and bounded point position/color/intensity/radius/enabled
values. A disabled point contributes zero intensity. Entities additionally
accept `visible` and `spinRadiansPerSecond`; the reference scene exercises
parent/child propagation and light settings without renderer-specific names.
Transform, camera, material and point-light component schemas are exported;
the full scene envelope and directional light do not yet have authoring schemas.

The separate `GltfMesh` importer accepts glTF 2.0 JSON plus a caller-resolved
binary buffer, validates before exposing CPU mesh/material data, and never
performs URI/network access or GPU allocation. Supported geometry is bounded
triangle primitives with float positions/normals/UV/colors, interleaved strides
and unsigned 8/16/32-bit indices; basic material factors and submesh material
indices are retained. Caps: 2048 vertices, 6144 indices, 32 primitives and 16
materials. Failed imports expose no partial geometry. Tests cover strided
normal/UV preservation, truncation, invalid indices and required extensions.

GLB, sparse/normalized accessors, texture binding, normal maps, skinning, morphs,
scene-node transforms, animation and modular importer registration are not
implemented. The demonstration deliberately remains procedural; no custom
model format or protected sample artwork was introduced.
