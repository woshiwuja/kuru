#pragma once
#include <SDL3/SDL_events.h>
struct EventManager {
  SDL_Event e;
  bool poll();
  void processInput(float deltaTime);
};
