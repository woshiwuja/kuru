#include "window.hpp"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
void Window::init() {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  window = SDL_CreateWindow(title.c_str(), width, height,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
}
void Window::quit() {
  SDL_DestroyWindow(window);
  SDL_Quit();
}
