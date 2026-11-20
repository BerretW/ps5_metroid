# Porting and new games

Use the existing CMake target rather than copying PROJECT ECHO gameplay:

```cmake
add_subdirectory(path/to/ps5_metroid/framework framework)
target_link_libraries(my_game PRIVATE EchoFramework::Core)
target_link_libraries(my_game PRIVATE EchoFramework::Entities EchoFramework::Events)
target_link_libraries(my_game PRIVATE EchoFramework::Data)
```

Link `EchoFramework::Data` only when needed. The core targets use C++20 and static storage; they do not depend on SDL2. Platform code should supply file access and save-storage callbacks outside core. A platform application owns its event/render loop and calls fixed-step simulation; the framework does not start a competing loop.

The current native Windows PS5 build adds the framework through `platform/ps5/CMakeLists.txt`, links the static core/data targets into the existing SDL application, and retains the native `_start`, CRT, SDK, SDL fork, `-SkipPs5Keyboard`, and package identity. No PS5 save-storage implementation is enabled. Build/package validation is static and is not a substitute for an actual console test.

Windows:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

PS5 controller package:

```powershell
.\scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard
```

Never run the PS5 executable on Windows or re-enable the known-crashing PS5 keyboard path as part of a framework port.
