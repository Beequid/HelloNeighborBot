# Building HelloNeighorBot

HelloNeighorBot is an injectable internal speedrun bot for *Hello Neighbor* (Old
Patch 1.1.6), a 64-bit Unreal Engine 4 shipping build. The build produces two
targets:

* `HelloNeighorBot.dll` — the bot, injected into the game process.
* `injector.exe` — a small `LoadLibrary` injector for the DLL.

The build is driven by CMake (3.20+), targets **x64**, and uses C++17.

## Prerequisites

* **Visual Studio 2022** with the **Desktop development with C++** workload.
  This provides the MSVC toolchain and the CMake/MSBuild generators used below.
* The **Windows SDK** (installed as part of the Desktop C++ workload). The DLL
  links against `d3d11`, `dxgi`, `d3dcompiler`, `user32`, and `gdi32`, all of
  which ship with the Windows SDK.
* **CMake 3.20 or newer** (the bundled CMake in VS2022 satisfies this).
* **git** — required because the dependencies are pulled at configure time via
  CMake `FetchContent`. **Internet access is required on the first configure**
  while these are cloned:
  * MinHook `v1.3.3` — function hooking (the D3D11 `Present` hook).
  * nlohmann/json `v3.11.3` — config and route JSON parsing.
  * Dear ImGui `v1.90.9` — the in-game overlay menu.

  After the first successful configure, the dependencies are cached in the build
  tree and no further network access is needed.

## Building from the command line

Run these from the repository root (`E:\HelloNeighorBot`):

```sh
cmake -B build -A x64
cmake --build build --config Release
```

The first command configures the project into the `build/` directory using the
**x64** architecture and downloads the FetchContent dependencies. The second
command compiles the `Release` configuration.

### Outputs

After a successful Release build:

* `build/Release/HelloNeighorBot.dll`
* `build/Release/injector.exe`

The injector defaults to looking for `HelloNeighorBot.dll` next to itself, so
keeping the two files in the same folder is convenient.

### x64 is mandatory

The game is a 64-bit process, and a 32-bit DLL cannot be injected into it.
`CMakeLists.txt` enforces this: if CMake is not configured for a 64-bit
toolchain (`CMAKE_SIZEOF_VOID_P` is not 8), configuration **fails** with:

```
HelloNeighorBot must be built as x64 (the game is 64-bit). Re-run CMake with: -A x64
```

Always pass `-A x64` to the configure step. The project also fails to configure
on non-Windows platforms (`HelloNeighorBot only targets Windows.`).

## Building in Visual Studio (CMake integration)

VS2022 can open the project directly via its built-in CMake support — no
separate solution file is needed:

1. In Visual Studio, choose **File > Open > Folder...** and select the repository
   root (`E:\HelloNeighorBot`).
2. Visual Studio detects `CMakeLists.txt` and runs the CMake configure step
   automatically. (Watch the **Output** pane — the first configure clones the
   FetchContent dependencies and needs internet access.)
3. Select an **x64** configuration from the configuration dropdown (e.g. an
   `x64-Release` CMake configuration). Because of the x64 enforcement above, an
   x86 configuration will fail to configure.
4. Build with **Build > Build All** (Ctrl+Shift+B).

The targets `HelloNeighorBot.dll` and `injector.exe` are produced under Visual
Studio's CMake output directory for the selected configuration.

## Troubleshooting

* **FetchContent / network failures on first configure.** Symptoms are `git`
  clone errors or timeouts for `minhook`, `nlohmann/json`, or `imgui` during
  the `cmake -B build -A x64` step. Ensure `git` is installed and on `PATH`,
  that you have working internet access, and that any proxy/firewall allows
  cloning from `github.com`. Re-run the configure once connectivity is restored.
  Note `FETCHCONTENT_QUIET` is `OFF`, so the clone progress is visible in the
  CMake output to help diagnose where it stalls.

* **Missing Windows SDK.** If configuration or linking fails because the
  compiler/SDK cannot be found, or headers like the D3D11/DXGI headers are
  missing, install/repair the **Desktop development with C++** workload in the
  Visual Studio Installer (which includes the Windows SDK).

* **`d3d11` / `dxgi` (and `d3dcompiler`) link errors.** Unresolved external
  symbols for Direct3D/DXGI functions almost always mean the Windows SDK is not
  installed or not selected. The DLL links these libraries explicitly; a
  complete Windows SDK provides their import libraries. Reinstall/repair the
  Windows SDK via the Desktop C++ workload.

* **CMake refuses to configure (x64).** If you see the
  "must be built as x64" error, you configured with a 32-bit toolchain. Delete
  the `build/` directory and re-run `cmake -B build -A x64` (or select an x64
  configuration in Visual Studio).
