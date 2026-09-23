#pragma once
#include "camera.hpp"
#include <Kuru.h>
#include "raycast.hpp"
#include "entt/entity/fwd.hpp"

using namespace KR;
struct CharacterControllerPlugin : public Plugin{
    void init(entt::registry &r) override {
    }
    void update(entt::registry &r ) override {
        const auto &input = *Core::get()->eventManager;
        const auto &frame = r.ctx().get<FrameContext>();
        if (!input.down(SDL_BUTTON_LEFT) || frame.uiCapturesMouse) {
            return;
        }
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);
        for(auto [e,camera] : r.view<Camera, MainCamera>().each()){
            if (castRay(camera.position(), screenToWorld(frame, {mouseX, mouseY}))){};
        }
    }
};
