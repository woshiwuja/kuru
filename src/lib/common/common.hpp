#pragma once
#include "vertex.hpp"
#include <string>
#include <vulkan/vulkan_raii.hpp>
#include <Jolt/Jolt.h>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif
namespace KR {

    inline JPH::Vec3 glmVecToJPH(glm::vec3 v){
        return JPH::Vec3(v.x,v.y,v.z);
    }
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
// Descriptor pool ceiling, not a live count. Counts submeshes, not entities:
// a multi-material mesh (e.g. a character with separate body/claws/head/legs
// textures) consumes one descriptor set per submesh, not just one per entity.
constexpr uint32_t MAX_OBJECTS = 256;

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

uint32_t findMemoryType(vk::PhysicalDeviceMemoryProperties props,
                        uint32_t typeFilter, vk::MemoryPropertyFlags memProps);
void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags memProps, vk::raii::Buffer &buffer,
                  vk::raii::DeviceMemory &bufferMemory);

// CMake copies shaders/, models/ and textures/ next to the binary, so paths
// resolve against the executable rather than whatever the cwd happens to be.
std::string assetPath(const std::string &relative);

std::vector<char> readFile(const std::string &filename);
#if defined(__GNUC__) || defined(__clang__)
inline std::string demangle(const char* mangled) {
    int status = 0;
    std::unique_ptr<char, void(*)(void*)> res(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    return (status == 0) ? res.get() : mangled;
}
inline const char* cdemangle(const char* mangled) {
    thread_local std::string buf;
    int status = 0;
    std::unique_ptr<char, void(*)(void*)> res(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    buf = (status == 0) ? res.get() : mangled;
    return buf.c_str();
}
#else
inline std::string demangle(const char* mangled) {
    return mangled; // MSVC: typeid().name() è già human-readable
}
#endif

struct Selected {};
} // namespace KR
