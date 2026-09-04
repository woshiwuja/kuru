#include "event.hpp"

bool EventManager::poll() { return SDL_PollEvent(&e); }

void EventManager::processInput(float deltaTime) {
    if (e.type == SDL_EVENT_KEY_DOWN) {
      switch (e.key.key) {}
    };
}
