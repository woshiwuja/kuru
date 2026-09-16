#include <vulkan/vulkan_profiles.hpp>
namespace KR {
    enum WindowMode {
        windowed = 0,
        fullscreen = 1,
        borderless = 2
    };
    struct Settings {
        uint window_mode = 0;
        uint width = 1920;
        uint height = 1080;
    };
}
