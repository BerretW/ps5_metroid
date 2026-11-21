# Capability manifest

`exportCapabilityManifest` writes one bounded, deterministic JSON description of
the compiled framework and selected application fragments. It gathers module,
action, capability, event payload, content-schema, and runtime-feature metadata
from static descriptors; component schemas come from the supplied live
`ComponentTypeRegistry`. It neither loads code nor changes the modules compiled
into an application.

## Compatibility contract

The top-level `manifestVersion` describes this JSON document format.
`framework.apiVersion` identifies the public framework API, while
`framework.minimumApiVersion` is the minimum consumer API supported by this
manifest. Consumers should reject unsupported manifest/API versions and must
not infer executable support from descriptive metadata. In particular,
`modules[].selectable` indicates a module may be selected in content only when
that module is already compiled into the application; it is not dynamic loading.

Every identifier is unique within its descriptor collection. Modules, actions,
capabilities, events, content schemas, and features are sorted by ID before
serialization, so fragment order does not affect output. Module dependency and
provide lists and feature constraints are also sorted. Property and action
parameter order remains the order declared by their static descriptor.
Duplicate IDs, malformed metadata, undeclared action capabilities, capacity
exhaustion, and insufficient output buffers return an explicit `Status`; failed
exports leave the output empty.

The exporter is bounded: at most 64 modules, 128 capabilities, 128 actions,
128 events, 64 content schemas, and 64 runtime features. Module lists,
parameter lists, and feature constraints each have a 32-entry limit. Component
schema capacities and JSON limits are defined in
[component schemas](COMPONENT_SCHEMAS.md) and [data formats](DATA_FORMATS.md).
The exporter uses a 64 KiB internal component-manifest buffer; consumers choose
the final output capacity and handle `BufferTooSmall`.

The manifest reports framework-provided condition operators and content
resource types, as well as a compatibility constraint list. Those entries are
descriptive contracts, not validation of every content document. Use
`echo_content_validate` to validate resource references and the owning
application's validator for application-specific content.

## Native host export

Enable the opt-in host tools and build the generic framework manifest:

```powershell
cmake -S . -B build/framework-tools -DECHO_FRAMEWORK_ENABLE_DATA=ON -DECHO_FRAMEWORK_ENABLE_NARRATIVE=ON -DECHO_FRAMEWORK_BUILD_TOOLS=ON -DBUILD_TESTING=OFF
cmake --build build/framework-tools --config Release
.\build\framework-tools\framework\tools\Release\echo_schema_export.exe framework-manifest.json
```

In the PROJECT ECHO root build, the project exporter additionally registers
compiled project component, action, event, schema, and module metadata:

```powershell
.\build\windows\echo_project_schema_export.exe project-manifest.json
```

For a Ninja single-configuration build, omit the `Release\` directory from
multi-configuration executable paths.

These are native host utilities. They export metadata only and do not load
runtime code from JSON. A successful export proves that the manifest is
syntactically valid and the registered metadata passed exporter checks; it does
not prove runtime feature completeness or PS5 hardware behavior.

The generic component editor accepts `--manifest <manifest.json>` to list
component IDs from the manifest, or `--manifest <manifest.json> <component-id>
<config.json> [resource-manifest.json]` to edit and validate a selected entry.
It rejects unsupported manifest/API versions, unsorted or duplicate component
IDs, component-version mismatches, and any component absent from its compiled
registry. The manifest only controls discovery and selection; static registry
descriptors remain authoritative for validation and factory invocation.

## PROJECT ECHO content validation

The project-specific host tools are built in the root Windows build when
`ECHO_FRAMEWORK_BUILD_TOOLS` is enabled. Export the compiled project manifest,
then validate the checked-in game configuration and dialogue:

```powershell
.\build\windows\echo_project_schema_export.exe build\windows\project-capability-manifest.json
.\build\windows\echo_project_content_validate.exe assets\framework\game.json assets\framework\engineer.json assets\framework\items.json build\windows\project-capability-manifest.json
```

The validator enforces the same 16 KiB file limits as runtime integration and
checks the complete compiled module/action/component descriptors, required
resource graph, technician configurations, Dialogue document ID/graph/actions,
text limits supported by the game's bitmap font, and the versioned item catalog
selected by the reference game. The PowerShell CTest
integration test verifies valid checked-in data and rejects invalid modules,
components, dependencies, dialogue actions/types, stale manifests, and oversized
input. This is static host-side validation; runtime loading and PS5 hardware
behavior remain separate.
