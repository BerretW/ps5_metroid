# Bounded CPU particle effects

`EchoFramework::VFX` provides a deterministic CPU particle module named
`echo.vfx.cpu_particles`, with the service `echo.service.vfx.cpu_particles`.
Add a static `echo::vfx::VfxModule` to the framework's `ModuleRegistry` when
effects are selected. Its `system()` accessor exposes the owned
`ParticleSystem`.

The system has at most 16 emitters and 256 particles per emitter. Create an
emitter from a validated `EmitterConfig`, call `emitBurst` for explicit bursts,
and call `update(fixedDeltaSeconds)` once per fixed simulation step. The
update accepts finite positive steps up to 0.25 seconds; games should pass
their own fixed step rather than frame time. Seeded xorshift generation,
bounded particle arrays, stable iteration, and fixed-step inputs make repeated
simulations deterministic. Continuous emitters use an emission accumulator;
particles beyond emitter capacity are dropped rather than queued without
bound.

`setGravity` applies a finite gravity vector immediately or linearly transitions
to it over a bounded duration. `particles(handle)` returns a read-only span
valid until the next mutation/update. Destroying an emitter invalidates its
generation handle. Configuration cannot exceed the particle, lifetime, rate,
velocity, or gravity bounds.

Each `EmitterConfig` has a bounded world-space `position`; it is the origin for
new particles. Each particle exposes interpolated RGBA `color` and uniform
`scale` values from the configured start to end values across its lifetime.
This lets an application pass per-particle tint and scale to its renderer
without creating a material per particle. `setPosition` moves the emitter
origin for future particles and leaves existing particle positions unchanged.

`EmitterDocument` parses a strict schema-version-1 JSON config through
`JsonDocument`, validates it with the same `EmitterConfig` bounds, and retains
the definition ID for the document lifetime. The sample
`experiments/vulkan3d/assets/reactor_sparks.json` includes continuous emission,
position, velocity spread, gravity, color/scale transitions, and a deterministic
seed.

This is CPU simulation only. It does not expose a compute shader or GPU
particle buffer. PROJECT ECHO's production game translates shot-emitter
particles into small alpha-blended rectangles through its existing 2D canvas;
the SDL and Vulkan game paths therefore share the same simulation and effect.
The current Vulkan backend reports compute, postprocessing, render to texture,
instancing, and shadow maps as unavailable.

Screen flash, camera shake, and light-event coupling remain deferred effects;
the module currently owns particles and emitter simulation only.

The standalone reference application explicitly translates live particles into
additively blended Vulkan quads using per-draw tint/scale. Interactive updates
use a 60 Hz accumulator with a 0.25-second catch-up limit; bounded `--frames`
runs deliberately advance one fixed step per frame for reproducible captures.
This is **CPU simulation with GPU rasterization**, not compute particles. The
production shot sparks use the fixed gameplay step and canvas alpha blending.
