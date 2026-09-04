#include "event.hpp"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

void EventManager::pump() {
  // Deltas are per frame; the button and key states are levels and persist.
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
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button == SDL_BUTTON_MIDDLE) {
        middleDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      }
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
}

bool EventManager::down(SDL_Scancode key) const {
  return keys != nullptr && keys[key];
}
