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
or render-target handle for `Textured`. Resource lifetime and actual GPU
allocations belong to the selected backend.

`FramePassScheduler` provides a fixed-capacity, SDL-free ordering of callbacks
across begin-frame, shadow, opaque, transparent, world-sprite, particle,
postprocessing, UI, and presentation stages. Explicit dependencies refine ordering within a stage;
declared resource reads and writes add read-after-write, write-after-read, and
write-after-write ordering. Resources may be marked initialized on entry for
external inputs; a pass cannot read and write the same resource, and a read of
an uninitialized resource fails before its callback. Disabled writers do not
initialize outputs. Optional disabled passes are skipped, and cycles or
callback errors are returned as `Status`. The scheduler still does not own
backend frame lifecycle or issue GPU barriers; backend callbacks remain
responsible for those operations. The Windows GPU diagnostic declares its
source-target write and sampling-target read/write, alongside its separate
feedback rejection check. The regular demo presentation loop now uses scheduled
begin-frame, opaque, transparent, particle, UI, and presentation callbacks.

`EntityStore` is a fixed-capacity 128-entity hierarchy. Handles include slot
generation; destroying a parent detaches its direct children while preserving
their local transforms. Parent assignment rejects cycles, and world matrix
propagation checks the chain again before composing each local
`translation * rotation * scaling` matrix.

Capability JSON is written into a caller-provided buffer. Core math,
validation, scene records, and VFX use bounded arrays/spans and do not allocate
in their update paths. The Vulkan experiment reports offscreen color/depth
render targets and target sampling as supported capabilities. Instancing, shadow maps,
postprocessing, and compute remain unsupported.

## Windows GPU implementation

`EchoFramework::RenderingVulkan` implements instance/device/Win32 surface,
hardware device selection, swapchain, depth, graphics pipelines, descriptors,
device-local vertex/index buffers, sRGB base textures and staging uploads.
Its platform surface adapter is separate; gameplay never owns Vulkan handles.
The Windows diagnostic and opt-in PS5 Vulkan game link these targets explicitly.
The original SDL renderer remains the default for the other builds.

Backend limits are 64 meshes, 64 materials, 64 textures, 8 render targets and
512 queued draws; capability JSON includes the render-target limit.
Resource handles have generations; destroying a texture referenced by a live
material fails explicitly. Recording forbids resource mutation. One frame is
in flight, with fence-serialized depth/uniform reuse and per-swapchain-image
presentation semaphores. Surface resize waits for idle and recreates dependent
framebuffers; offscreen targets have independent dimensions and transactional
resize, device-local color/depth attachments, sampled color usage, and GPU
memory accounting. `beginTargetFrame` records a regular scene into a target;
`endFrame` submits it without acquiring or presenting the swapchain and makes
the color image shader-readable. Target handles are generation-checked and
may be explicitly destroyed; backend shutdown also releases them.
`Material::targetTexture` samples a completed target as its base texture in a
later pass. Live materials retain that target, resize refreshes their
descriptors after the GPU is idle, uninitialized sources and same-target
feedback are rejected, and a referenced target cannot be destroyed.
Diagnostic readback verifies source color/depth and sampled output on Windows.
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
handles retain the existing generation and lifecycle checks. This is a
portable `IRenderBackend` operation; other backends must implement it or return
an explicit unsupported error.
The GPU probe updates a mesh over 60 frames, checks separate index ranges
and verifies the last frame by readback. This is not a performance benchmark.

The standalone Windows diagnostic records the actual game's menu and gameplay
geometry through the SDL-free `ICanvas2D` interface, batches geometry into a
dynamic mesh and draws it with Vulkan. With `--game --sprites` it supplies
backend-owned handles for the six original built-in sprite sheets.
`--game --sprites --data-world` loads the packaged game configuration and JSON
world, uploads its referenced world sprite sheets, and records the configured
starting room, including atlas frames and tiled layers. `CanvasWorldSprites`
associates validated asset-relative paths with backend handles; the recorder
preserves tile UV transforms, mirroring, rotation and isometric occlusion tint.
SDL tests compare recorded menus, options, inventory, DualSense Lab and fallback
gameplay with their SDL output, check failure propagation, and reject recording with loaded
SDL textures when no canvas sprites are supplied. A fixed-step diagnostic runs
the built-in game simulation; Windows GPU readback checks both player movement
and the original sprite. Vulkan blending is not
claimed to be pixel-identical to SDL, especially for translucent panels.

`ICanvas2D` describes ordered clears, rectangle fills, and textured quads in
caller-defined logical coordinates. Textured quads take a backend texture handle,
normalized UV transform, linear tint, and clockwise rotation in degrees. The
experimental Vulkan canvas bakes atlas UVs into vertices, rotates in logical
coordinates before viewport conversion, batches adjacent materials, and preserves submission
order; texture creation and lifetime remain the selected backend's responsibility.
The SDL test sink covers the same command shape. Clipping and frame lifecycle
are not part of this interface; the platform adapter loads resources and presents
the batched canvas via `IRenderBackend`. The game can be built with the PS5
Vulkan path or the existing SDL path, but the choice is build-time rather than
runtime. Screen recording still lives alongside SDL-specific rendering and input;
this is an incremental boundary, not a fully backend-independent 2D/3D API.

The Vulkan canvas test submits both a synthetic 1x1 GPU texture and the original
player PNG atlas through `ICanvas2D`, applies UV selection, tint and rotation,
and checks the GPU-captured pixels. The JSON-world GPU test loads the real content
configuration and world sprites, then compares its capture against the built-in
fallback. SDL and the diagnostic share the bounded `loadPngRgba` decoder.

Opaque draws write/test depth, alpha draws test without writes, additive
particle draws test without writes, and overlay draws bypass depth. The shader
uses bounded frame lighting and per-draw transform/material/tint parameters.
The demo uses perspective or isometric projection, a parented emissive cube,
a floor, translucent geometry, the original Echo sprite and numeric UI.

The PS5 candidate has an ICD dispatch and `VK_KHR_display` surface adapter. Its
offscreen target path uses the same Vulkan 1.0 graphics render pass and relies
on the PS5 driver supporting the requested image usages; target creation
reports unsupported formats or extents instead of claiming support for that
specific configuration. Console validation is still required. It
has not been exercised on PS5 as part of the full game. There is no general SDL
compatibility adapter, instanced draw path, GPU timer, built-in shadow or
postprocessing effect, or compute dispatch. A GPU mesh draw
currently uses one material for its complete index buffer; imported CPU submesh
material indices do not imply GPU multi-material submission. Device loss is an
explicit failure, not automatic recovery or a software fallback. The
hierarchy's handles are renderer-scene references, not a replacement gameplay
entity registry.
The Windows diagnostic remains separate from the game. The PS5 build connects
the production fixed-step loop, screen recording, controller, audio and saves to
the GPU canvas, but their behavior in this combination is not yet hardware
verified. The PS5 path currently renders the existing 2D game; it does not
enable the separate 3D/VFX demonstration in gameplay.

The opt-in host manifest exporter uses the existing unified manifest/schema
machinery and the actual `VfxModule::info()` descriptor. Rendering is currently
a library/backend, not an invented dynamically loadable module. The VFX module
has explicit static metadata and no required external services; see [VFX](VFX.md).
