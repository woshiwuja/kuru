cmake_minimum_required(VERSION 3.20)
project(Kuru CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.16
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
    DirectXHeaders
    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers.git
    GIT_TAG v1.619.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(DirectXHeaders)

add_executable(Kuru
    src/main.cpp
    src/lib/core/core.cpp
)

target_include_directories(Kuru PRIVATE src)

target_link_libraries(Kuru PRIVATE d3d12 dxgi d3dcompiler SDL3::SDL3 Microsoft::DirectX-Headers Microsoft::DirectX-Guids)

add_custom_command(TARGET Kuru POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:Kuru>
)
