# Shader contract

Original GLSL 450 vertex/fragment programs are compiled offline to Vulkan 1.1
SPIR-V by pinned native `glslangValidator`; `spirv-val --target-env vulkan1.1`
validates each output. No runtime GLSL compiler, sample shader copying or
compute stage is used.

- Vertex: position `vec3`, normal `vec3`, UV `vec2`, color `vec4`; 48-byte stride.
- Descriptor set 0 binding 0: 240-byte std140 camera/light uniform block, shared
  vertex/fragment stages.
- Binding 1: combined sRGB sampled image/sampler.
- Push constants: 112 bytes, model matrix, base color, emission/metallic,
  roughness/model/overlay controls. This is below Vulkan's mandatory 128-byte
  minimum. C++ size checks guard the ABI.
- Mesh projection: column-major, right-handed, depth 0..1 and Vulkan Y flip.
- Normals: inverse-transpose model matrix. Singular mesh transforms are rejected.
- Materials: unlit, vertex-color, textured Lambert, simple metallic/roughness
  Cook-Torrance approximation (GGX/Schlick), ambient, one directional light,
  four bounded point lights and emission.
- sRGB texture decode and framebuffer encode are performed by Vulkan formats.
  Lighting calculations remain linear.
- Opaque depth test/write; transparent/additive depth test without writes;
  overlay alpha blend without depth. Transparent meshes sort back-to-front.

No normal mapping, IBL, shadow pass, bloom/blur or shader hot reload is claimed.
SPIR-V size/magic are checked before module creation; driver pipeline failures
name the exact Vulkan call and result.
