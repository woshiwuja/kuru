#pragma  once
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <string>
struct Window{
    int width = 800;
    int height = 600;
    std::string title = "Kuru App";
    SDL_Window* window = nullptr;
    void init();
    void quit();
};
