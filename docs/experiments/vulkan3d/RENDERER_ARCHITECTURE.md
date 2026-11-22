# Vulkan experiment rendering core

The reusable rendering layer in `EchoFramework::Rendering` is CPU-only and
does not include SDL, Vulkan, or platform headers. `IRenderBackend` is the
boundary for platform implementations; math, mesh/material validation,
capability serialization, sorting, scene metadata, and transform hierarchy
remain reusable in headless code.

Matrices are 16-float, column-major matrices. The camera convention is
right-handed with Y up. `lookAt` follows the standard right-handed view basis.
`perspective` and `orthographic` map depth to Vulkan's `[0,1]` clip interval
and negate the projection Y scale for a positive-height Vulkan viewport.
Perspective input uses radians, a positive aspect ratio, and `far > near > 0`.

Use `drawBefore` to sort a frame: opaque draws precede transparent draws,
transparent draws are ordered back-to-front by camera distance, and overlays
are last. The function is a comparator; use a stable sort to preserve submission
order for ties.

The CPU mesh boundary caps buffers at 65,536 vertices, 196,608 indices, and
256 submeshes. Meshes are triangles, indices must resolve to vertices, and
vertex fields must be finite and bounded. Materials validate finite normalized
color, emission and PBR ranges, enum values, and require a valid texture handle
for `Textured`. Resource lifetime and actual GPU allocations belong to the
selected backend.

`EntityStore` is a fixed-capacity 128-entity hierarchy. Handles include slot
generation; destroying a parent detaches its direct children while preserving
their local transforms. Parent assignment rejects cycles, and world matrix
propagation checks the chain again before composing each local
`translation * rotation * scaling` matrix.

Capability JSON is written into a caller-provided buffer. Core math,
validation, scene records, and VFX use bounded arrays/spans and do not allocate
in their update paths. The current Vulkan experiment reports render-to-texture,
instancing, shadow maps, postprocessing, and compute as unsupported.

## Windows GPU implementation

`EchoFramework::RenderingVulkan` implements instance/device/Win32 surface,
hardware device selection, swapchain, depth, graphics pipelines, descriptors,
device-local vertex/index buffers, sRGB base textures and staging uploads.
Its platform surface adapter is separate; gameplay never owns Vulkan handles.
The independent experiment links these targets explicitly, without modifying
the production framework selection or forcing SDL rendering through this API.

Backend limits are 64 meshes, 64 materials, 64 textures and 512 queued draws.
Resource handles have generations; destroying a texture referenced by a live
material fails explicitly. Recording forbids resource mutation. One frame is
in flight, with fence-serialized depth/uniform reuse and per-swapchain-image
presentation semaphores. Resize waits for idle and recreates dependent targets.
Shutdown drains the device and destroys children before their device/instance;
validation errors during cleanup also fail the integration run.

Per-draw UV offset and scale select a sprite-sheet region without allocating
a separate mesh for each frame. `spriteUvTransform` validates pixel bounds and
supports horizontal and vertical mirroring; invalid regions leave the output
unchanged. The shader and backend share a 128-byte push-constant layout. The
Windows experiment uses this path for its original player sprite and verifies
cropped and mirrored synthetic atlas pixels via GPU readback.

The Windows Vulkan backend also supports a bounded dynamic mesh for batched
2D geometry. Its persistently mapped coherent vertex/index buffers may be
updated only between frames, after waiting for the previous submission fence.
Updates validate the entire mesh and capacity before changing either buffer;
handles retain the existing generation and lifecycle checks. This is an
explicit Vulkan-backend API, not yet a portable `IRenderBackend` capability.
The GPU probe updates a mesh over 60 frames, checks separate index ranges
and verifies the last frame by readback. This is not a performance benchmark.

The standalone Windows diagnostic records the actual game's menu and fallback
gameplay geometry through `DrawSink`, batches colored rectangles into a dynamic
mesh and draws them with Vulkan. The production SDL path remains the default.
SDL tests compare recorded menus and fallback gameplay pixel-for-pixel with
their SDL output, check failure propagation, and reject recording with loaded
SDL textures. A fixed-step diagnostic runs the built-in game simulation;
Windows GPU readback checks that its player moves. Vulkan blending is not
claimed to be pixel-identical to SDL, especially for translucent panels.

Opaque draws write/test depth, alpha draws test without writes, additive
particle draws test without writes, and overlay draws bypass depth. The shader
uses bounded frame lighting and per-draw transform/material/tint parameters.
The demo uses perspective or isometric projection, a parented emissive cube,
a floor, translucent geometry, the original Echo sprite and numeric UI.

There is no native PS5 presentation adapter, SDL compatibility adapter,
offscreen render-target/viewport binding API, instanced draw path, GPU timer,
shadow/postprocessing path or compute dispatch. A GPU mesh draw currently uses
one material for its complete index buffer; imported CPU submesh material
indices do not imply GPU multi-material submission. Device loss is an explicit
failure, not automatic recovery or a software fallback. The hierarchy's handles
are renderer-scene references, not a replacement gameplay entity registry.
The diagnostic uses built-in fallback art and world data, and supports only
limited keyboard controls. Other screens, original sprites, game content,
audio, controller input, saves, the production game loop and the PS5 adapter
have not yet been connected to Vulkan. It is not a complete game renderer or
a PS5 hardware result.

The opt-in host manifest exporter uses the existing unified manifest/schema
machinery and the actual `VfxModule::info()` descriptor. Rendering is currently
a library/backend, not an invented dynamically loadable module. The VFX module
has explicit static metadata and no required external services; see [VFX](VFX.md).
