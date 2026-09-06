cmake_minimum_required(VERSION 3.28)
project(Kuru C CXX) # C for libktx

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Vulkan SDK unpacked in the project root; FindVulkan picks it up via VULKAN_SDK.
file(GLOB _sdk "${CMAKE_SOURCE_DIR}/vulkansdk-linux-x86_64-*/*/x86_64")
if(_sdk AND NOT DEFINED ENV{VULKAN_SDK})
  list(GET _sdk 0 _sdk)
  set(ENV{VULKAN_SDK} "${_sdk}")
  # loader ships under lib/VulkanLoader/lib, not where FindVulkan looks
  list(APPEND CMAKE_PREFIX_PATH "${_sdk}/lib/VulkanLoader")
endif()
find_package(Vulkan REQUIRED)

if(MSVC)
  # rc.exe (used for SDL3's version.rc) has no toolchain auto-detection like clang-cl does;
  # it only finds SDK headers (winresrc.h) via explicit -I, so point it at the SDK CMAKE_RC_COMPILER resolved.
  get_filename_component(_rc_ver_dir "${CMAKE_RC_COMPILER}" DIRECTORY) # .../bin/<ver>/x64
  get_filename_component(_rc_ver_dir "${_rc_ver_dir}" DIRECTORY)       # .../bin/<ver>
  get_filename_component(_sdk_ver "${_rc_ver_dir}" NAME)
  get_filename_component(_sdk_root "${_rc_ver_dir}" DIRECTORY)         # .../bin
  get_filename_component(_sdk_root "${_sdk_root}" DIRECTORY)           # .../Windows Kits/10
  set(_sdk_include "${_sdk_root}/Include/${_sdk_ver}")
  if(EXISTS "${_sdk_include}/um/winresrc.h")
    string(APPEND CMAKE_RC_FLAGS " /I \"${_sdk_include}/um\" /I \"${_sdk_include}/shared\" /I \"${_sdk_include}/ucrt\"")
  endif()
endif()

include(FetchContent)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_WAYLAND OFF) # ponytail: X11 only; flip on when wayland dev pkgs are around
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)
FetchContent_Declare(entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.15.0
  GIT_SHALLOW TRUE)
# header-only: SOURCE_SUBDIR has no CMakeLists, so these only get downloaded
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20 # no upstream tags
  SOURCE_SUBDIR no-configure)
FetchContent_Declare(tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG v2.9.7 # v3 is a C rewrite with a different API; v2 is the header-only tiny_gltf.h
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-configure)
set(KTX_FEATURE_TESTS OFF)
set(KTX_FEATURE_TOOLS OFF)
set(KTX_FEATURE_DOC OFF)
set(KTX_FEATURE_LOADTEST_APPS OFF)
set(KTX_FEATURE_GL_UPLOAD OFF) # Vulkan only
set(BUILD_SHARED_LIBS OFF) # no .so to ship next to the binary
FetchContent_Declare(ktx
  GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
  GIT_TAG v4.4.2
  GIT_SHALLOW TRUE)
set(SDL_TEST_LIBRARY OFF)
# ponytail: optional X11 extras whose -dev packages aren't installed here
set(SDL_X11_XSCRNSAVER OFF)
set(SDL_X11_XTEST OFF)
set(SDL_EXAMPLES OFF)
FetchContent_Declare(sdl3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.16
  GIT_SHALLOW TRUE)
# header/source drop, no CMakeLists of its own
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b-docking
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-configure)
# Jolt defaults to the static MSVC CRT (/MT), which mismatches everything else here (/MD) at link time
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
# Jolt's CMakeLists lives in Build/, not the repo root
FetchContent_Declare(jolt
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.6.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR Build)
# CACHE ... FORCE: v1.6.0's cmake_minimum_required(3.1) defaults CMP0077 to OLD,
# so option() ignores a plain set() and would otherwise pull in the SDL2 demo
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
# ponytail: v1.6.0's cmake_minimum_required(3.1) predates CMake 4's removal of <3.5 support
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(recastnavigation
  GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
  GIT_TAG v1.6.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm glfw entt stb tinygltf ktx sdl3 imgui jolt recastnavigation)

file(GLOB_RECURSE KURU_SRC src/lib/*.cpp src/game/*.cpp)
set(IMGUI_SRC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp # ui.cpp calls ImGui::ShowDemoWindow
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
add_executable(Kuru src/main.cpp ${KURU_SRC} ${IMGUI_SRC})
target_link_libraries(Kuru PRIVATE Vulkan::Vulkan glfw SDL3::SDL3 glm::glm EnTT::EnTT ktx
  Jolt::Jolt RecastNavigation::Recast RecastNavigation::Detour
  RecastNavigation::DetourCrowd RecastNavigation::DetourTileCache RecastNavigation::DebugUtils)
# stb first: tinygltf vendors an older stb_image.h and this shadows it with one copy
target_include_directories(Kuru PRIVATE ${stb_SOURCE_DIR} ${tinygltf_SOURCE_DIR}
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# main.cpp initialises vk structs with designated initialisers
# main.cpp expects the tinygltf impl macro on the command line (see its include block)
target_compile_definitions(Kuru PRIVATE VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
  TINYGLTF_IMPLEMENTATION STB_IMAGE_IMPLEMENTATION)

# main.cpp opens textures/ and models/ cwd-relative too. ponytail: configure-time copy, static assets
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures ${CMAKE_CURRENT_SOURCE_DIR}/models
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# main.cpp opens shaders/slang.spv relative to the cwd, so emit it next to the binary.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders) # slangc won't create it
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/slang.spv
  COMMAND $ENV{VULKAN_SDK}/bin/slangc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/slang.slang
          -target spirv -emit-spirv-directly -fvk-use-entrypoint-name
          -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/slang.spv
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/slang.slang
  VERBATIM)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/sky_clouds.spv
  COMMAND $ENV{VULKAN_SDK}/bin/slangc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_clouds.slang
          -target spirv -emit-spirv-directly -fvk-use-entrypoint-name
          -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/sky_clouds.spv
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_clouds.slang
  VERBATIM)
add_custom_target(shaders DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shaders/slang.spv
                                  ${CMAKE_CURRENT_BINARY_DIR}/shaders/sky_clouds.spv)
add_dependencies(Kuru shaders)

# editor tooling (clangd, zed) looks for compile_commands.json at the repo root
add_custom_target(compile_commands ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json)
add_dependencies(Kuru compile_commands)
