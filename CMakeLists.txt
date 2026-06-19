cmake_minimum_required(VERSION 3.20)
project(HelloNeighorBot LANGUAGES CXX)

# ---------------------------------------------------------------------------
# HelloNeighorBot - an injectable UE4 speedrun bot for Hello Neighbor (1.1.6).
#
# Produces two targets:
#   * HelloNeighorBot.dll  - the bot, injected into the game process.
#   * injector.exe         - a small LoadLibrary injector for the DLL.
#
# The game is a 64-bit Unreal Engine 4 shipping build, so the DLL MUST be
# built for x64. Configure with the x64 toolchain, e.g.:
#   cmake -B build -A x64
#   cmake --build build --config Release
# ---------------------------------------------------------------------------

if(NOT WIN32)
  message(FATAL_ERROR "HelloNeighorBot only targets Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The target game is x64. A 32-bit DLL cannot be injected into a 64-bit process.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "HelloNeighorBot must be built as x64 (the game is 64-bit). "
    "Re-run CMake with: -A x64")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- MinHook: function hooking (used for the D3D11 Present hook) ------------
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        v1.3.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(minhook)

# --- nlohmann/json: config + route parsing ---------------------------------
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# --- Dear ImGui: in-game overlay menu --------------------------------------
# ImGui ships no CMakeLists, so FetchContent only downloads it; we compile the
# core + Win32/DX11 backends into a static lib ourselves.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.90.9
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)

# --- The bot DLL -----------------------------------------------------------
file(GLOB_RECURSE BOT_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/dll/*.cpp")

add_library(HelloNeighorBot SHARED ${BOT_SOURCES})
target_include_directories(HelloNeighorBot PRIVATE "${CMAKE_SOURCE_DIR}/src/dll")
target_compile_definitions(HelloNeighorBot PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(HelloNeighorBot PRIVATE
  minhook
  nlohmann_json::nlohmann_json
  imgui
  d3d11 dxgi d3dcompiler
  user32 gdi32)
set_target_properties(HelloNeighorBot PROPERTIES
  PREFIX ""
  OUTPUT_NAME "HelloNeighorBot")

# --- The injector exe ------------------------------------------------------
add_executable(injector "${CMAKE_SOURCE_DIR}/src/injector/injector.cpp")
target_compile_definitions(injector PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(injector PRIVATE user32)
