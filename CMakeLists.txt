cmake_minimum_required(VERSION 3.28)
project(Kuru C CXX) # C for libktx

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Everything that differs between the Windows and Linux builds lives here. Dependency
# options are only read at FetchContent_MakeAvailable, so they can all be set up front.
if(WIN32)
  # Jolt defaults to the static MSVC CRT (/MT), which mismatches everything else here (/MD) at link time
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
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
else()
  # Vulkan SDK unpacked in the project root; FindVulkan picks it up via VULKAN_SDK.
  # (On Windows the installer sets VULKAN_SDK itself, so there is nothing to find.)
  file(GLOB _sdk "${CMAKE_SOURCE_DIR}/vulkansdk-linux-x86_64-*/*/x86_64")
  if(_sdk AND NOT DEFINED ENV{VULKAN_SDK})
    list(GET _sdk 0 _sdk)
    set(ENV{VULKAN_SDK} "${_sdk}")
    # loader ships under lib/VulkanLoader/lib, not where FindVulkan looks
    list(APPEND CMAKE_PREFIX_PATH "${_sdk}/lib/VulkanLoader")
  endif()
  # ponytail: optional X11 extras whose -dev packages aren't installed here
  set(SDL_X11_XSCRNSAVER OFF)
  set(SDL_X11_XTEST OFF)
endif()
find_package(Vulkan REQUIRED)

set(CMAKE_POSITION_INDEPENDENT_CODE ON) # KuruLib is shared; its static deps must be too

include(FetchContent)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
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
# Jolt's Vulkan backend adds Vulkan_INCLUDE_DIRS as a PUBLIC include dir, which CMake
# rejects because the SDK sits inside our source tree. ponytail: we don't use Jolt on the GPU
set(JPH_USE_VK OFF)
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
FetchContent_MakeAvailable(glm entt stb tinygltf ktx sdl3 imgui jolt recastnavigation)

file(GLOB_RECURSE KURU_LIB_SRC src/lib/*.cpp)
file(GLOB_RECURSE KURU_GAME_SRC src/game/*.cpp)
set(IMGUI_SRC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp # ui.cpp calls ImGui::ShowDemoWindow
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp # InputText on std::string
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

# The engine (namespace KR), consumed through src/lib/Kuru.h.
# WINDOWS_EXPORT_ALL_SYMBOLS: no per-symbol __declspec sprinkling; CMake writes
# the .def. It only covers functions, so the library exposes no extern data.
add_library(KuruLib SHARED ${KURU_LIB_SRC})
set_target_properties(KuruLib PROPERTIES OUTPUT_NAME Kuru WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(KuruLib PUBLIC src/lib)
# PUBLIC: these all appear in Kuru.h's own headers, so consumers need them too.
target_link_libraries(KuruLib PUBLIC Vulkan::Vulkan SDL3::SDL3 glm::glm EnTT::EnTT Jolt::Jolt
  PRIVATE ktx)
# stb first: tinygltf vendors an older stb_image.h and this shadows it with one copy
target_include_directories(KuruLib PRIVATE ${stb_SOURCE_DIR} ${tinygltf_SOURCE_DIR})
# The first two change struct layout/ABI, so they have to match in the game too.
# model.cpp/image.cpp hold the single header-only implementation.
target_compile_definitions(KuruLib
  PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS GLM_FORCE_DEPTH_ZERO_TO_ONE
  PRIVATE TINYGLTF_IMPLEMENTATION STB_IMAGE_IMPLEMENTATION)

add_executable(Kuru ${KURU_GAME_SRC} ${IMGUI_SRC})
target_link_libraries(Kuru PRIVATE KuruLib
  RecastNavigation::Recast RecastNavigation::Detour
  RecastNavigation::DetourCrowd RecastNavigation::DetourTileCache RecastNavigation::DebugUtils)
target_include_directories(Kuru PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(MSVC)
  # clang-cl/cl.exe default to exceptions off; Kuru's error paths (and tinygltf/nlohmann json) use throw/try.
  # PRIVATE: Jolt's own CMakeLists disables exceptions for itself and must not see this.
  target_compile_options(KuruLib PRIVATE /EHsc)
  target_compile_options(Kuru PRIVATE /EHsc)
endif()

# main.cpp opens textures/ and models/ cwd-relative too. ponytail: configure-time copy, static assets
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures ${CMAKE_CURRENT_SOURCE_DIR}/assets/models
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# render.cpp opens shaders/*.spv relative to the cwd, so emit them next to the binary.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders) # slangc won't create it
foreach(_shader slang sky_clouds)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${_shader}.spv
    COMMAND $ENV{VULKAN_SDK}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${_shader}.slang
            -target spirv -emit-spirv-directly -fvk-use-entrypoint-name
            -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/${_shader}.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${_shader}.slang
    VERBATIM)
  list(APPEND _spv ${CMAKE_CURRENT_BINARY_DIR}/shaders/${_shader}.spv)
endforeach()
add_custom_target(shaders DEPENDS ${_spv})
add_dependencies(Kuru shaders)

# editor tooling (clangd, zed) looks for compile_commands.json at the repo root
add_custom_target(compile_commands ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json)
add_dependencies(Kuru compile_commands)
