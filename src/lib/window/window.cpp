#include "window.hpp"
#include "../common/common.hpp" // assetPath: every other loader resolves against the exe dir
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"
#include "SDL3_image/SDL_image.h"
#include <iostream>

namespace KR {
void Window::init() {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  window = SDL_CreateWindow(title.c_str(), width, height,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | modeToSDL(mode) );
  apply(); 
  setCursor("cursors/green.cur");
}
void Window::apply() {
  if (mode == windowed) {
    SDL_SetWindowFullscreen(window, false);
    SDL_SetWindowSize(window, width, height);
    return;
  }
  if (mode == borderless) {
    SDL_SetWindowFullscreenMode(window, nullptr);
    SDL_SetWindowFullscreen(window, true);
    return;
  }
  SDL_DisplayMode closest{};
  if (SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),
                                          width, height, 0.0f, true,
                                          &closest)) {
    SDL_SetWindowFullscreenMode(window, &closest);
  } else {
    std::cerr << "[warn] Window::apply: no " << width << "x" << height
              << " display mode, using desktop: " << SDL_GetError() << '\n';
    SDL_SetWindowFullscreenMode(window, nullptr);
  }
  SDL_SetWindowFullscreen(window, true);
}

void Window::quit() {
  SDL_SetCursor(SDL_GetDefaultCursor());
  SDL_DestroyCursor(cursor);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
void Window::setCursor(const char *path) {
  auto s = IMG_Load(assetPath(path).c_str());
  if (!s) {
    std::cerr << "[warn] setCursor: " << SDL_GetError() << '\n';
    return;
  }
  const auto props = SDL_GetSurfaceProperties(s);
  auto *previous = cursor;
  cursor = SDL_CreateColorCursor(
      s, (int)SDL_GetNumberProperty(props, SDL_PROP_SURFACE_HOTSPOT_X_NUMBER, 0),
      (int)SDL_GetNumberProperty(props, SDL_PROP_SURFACE_HOTSPOT_Y_NUMBER, 0));
  SDL_DestroySurface(s);
  SDL_SetCursor(cursor);
  SDL_DestroyCursor(previous);
}
} // namespace KR
