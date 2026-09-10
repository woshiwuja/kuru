#include "event.hpp"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

void EventManager::pump() {
  mouseDeltaX = 0.0f;
  mouseDeltaY = 0.0f;
  wheel = 0.0f;
  events.clear();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    events.push_back(event);
    switch (event.type) {
    case SDL_EVENT_QUIT:
      quit = true;
      break;
    case SDL_EVENT_MOUSE_MOTION:
      mouseDeltaX += event.motion.xrel;
      mouseDeltaY += event.motion.yrel;
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      wheel += event.wheel.y;
      break;
    case SDL_EVENT_KEY_DOWN:
      if (event.key.key == SDLK_ESCAPE) {
        quit = true;
      }
      break;
    default:
      break;
    }
  }
  keys = SDL_GetKeyboardState(nullptr);
  buttons = SDL_GetMouseState(nullptr, nullptr);
}

bool EventManager::down(SDL_Scancode key) const {
  return keys != nullptr && keys[key];
}

bool EventManager::down(int mouseButton) const {
  return (buttons & SDL_BUTTON_MASK(mouseButton)) != 0;
}
