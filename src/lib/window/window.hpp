#pragma  once
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <string>

enum WindowMode { windowed = 0, fullscreen = 1, borderless = 2 };
namespace KR {
struct Window{
    int width = 800;
    int height = 600;
    WindowMode mode = windowed;
    std::string title = "Kuru App";
    SDL_Window* window = nullptr;
    SDL_Cursor *cursor = nullptr;
    void init();
    void quit();
    void setCursor(const char *path);
    void apply();
};

    inline uint64_t modeToSDL(WindowMode mode) {
        switch (mode)
        {
        case windowed:
        return 0;
            break;
        // Both start fullscreen: SDL's default is desktop-sized borderless,
        // apply() then picks a real display mode for exclusive fullscreen.
        // SDL_WINDOW_BORDERLESS would only drop the title bar, not fill the
        // screen.
        case fullscreen:
            return SDL_WINDOW_FULLSCREEN;
            break;
        case borderless:
            return SDL_WINDOW_FULLSCREEN;
            break;
        default:
        return 0;
            break;
        }
    }
} // namespace KR
