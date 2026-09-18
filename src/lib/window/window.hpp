#pragma  once
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <string>

namespace KR {
struct Window{
    int width = 800;
    int height = 600;
    std::string title = "Kuru App";
    SDL_Window* window = nullptr;
    SDL_Cursor *cursor = nullptr;
    void init();
    void quit();
    void setCursor(const char *path);
};
} // namespace KR
