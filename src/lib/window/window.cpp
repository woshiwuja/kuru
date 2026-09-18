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
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  setCursor("cursors/green.cur");
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
  SDL_DestroyCursor(previous); // after SDL_SetCursor: destroying the live one is UB
}
} // namespace KR
