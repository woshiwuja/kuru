#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include <cstdint>
#include <string>
#include <filesystem>
#include "imgui.h"
#include "physics/raycast.hpp"
#include "../model/model.hpp"
#include "../render/render.hpp"
#include "../transform/transform.hpp"
#include "../ui/ui.hpp"
struct Item {
    std::string name = "Item";
    std::string description = "An item";
    std::string model = "models/default_item.glb";
    std::filesystem::path icon = "images/default_item.jpg";
    uint32_t amount = 1;
    uint32_t maxAmount = 64;
    float weight = 1.0f; //kg
};

struct InInventory {
    entt::entity owner = entt::null;
};

struct Weapon{};
struct Armor{};
struct Misc{};
struct Food{};

struct ItemPlugin : public KR::Plugin {
    ItemPlugin() {
        KR::registerComponent<Item>();
        KR::registerComponent<InInventory>();
    }

    // Spawning/despawning renderables only here: in update() the frame's
    // command buffer already references them (see NavigationPlugin::start).
    void start(entt::registry &r) override {
        auto &render = renderer(r);
        // Into an inventory: drop what the world needed, keep the Item.
        for (entt::entity e : ModelPlugin::collect(r.view<Item, InInventory, MeshRef>())) {
            render.despawnNodes(r, e);
            RenderPlugin::Retired old{.frame = render.frameCount};
            old.mesh = std::move(r.get<MeshRef>(e).mesh);
            if (auto *own = r.try_get<Renderable>(e)) {
                old.renderable = std::move(*own);
            }
            if (auto *material = r.try_get<MaterialRef>(e)) {
                old.material = std::move(material->material);
            }
            render.retired.push_back(std::move(old));
            // Removing BodyID takes the body out of Jolt (PhysicsPlugin::removeBody).
            r.remove<MeshRef, Renderable, MaterialRef, Model, Prop, Transform,
                     JPH::BodyID, JPH::BodyCreationSettings>(e);
        }
        // Out of any inventory and not in the world yet: ModelPlugin loads it.
        for (entt::entity e : ModelPlugin::collect(
                 r.view<Item>(entt::exclude<InInventory, MeshRef, ModelPath>))) {
            if (!r.all_of<Transform>(e)) {
                r.emplace<Transform>(e);
            }
            r.emplace<ModelPath>(e, r.get<Item>(e).model);
        }
        // Loaded: a body sized to the mesh. ModelPlugin::start ran first, so
        // this is the same frame as the load.
        for (entt::entity e : ModelPlugin::collect(r.view<Item, MeshRef, Transform>(
                 entt::exclude<InInventory, JPH::BodyID, JPH::BodyCreationSettings>))) {
            const Mesh &mesh = *r.get<MeshRef>(e).mesh;
            // ponytail: box centred on the origin, not on the mesh bounds;
            // wrap it in a RotatedTranslatedShape if models aren't centred.
            // 0.1 floor: Jolt asserts on sizes under the convex radius.
            const glm::vec3 half = glm::max((mesh.boundsMax - mesh.boundsMin) * 0.5f,
                                            glm::vec3(0.1f));
            auto &settings = r.emplace<JPH::BodyCreationSettings>(
                e, new JPH::BoxShape(glmVecToJPH(half)), JPH::RVec3::sZero(),
                JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, MOVING);
            // Items are thin: discrete steps tunnel them through the map.
            settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        }
    }

    void update(entt::registry &r) override {
        const auto &frame = r.ctx().get<FrameContext>();
        auto &event = Core::get()->eventManager;
        if (frame.uiCapturesKeyboard) {
            return;
        }
        if (!event->down(SDL_SCANCODE_LALT) && !event->down(SDL_SCANCODE_RALT)) {
            return;
        }
        auto bg = ImGui::GetBackgroundDrawList();
        ImGui::PushFont(findFont("Jersey10-Regular.ttf"), 24.0f);
        const ImVec2 pad{4.0f, 2.0f};
        // Items in an inventory have no Transform, so only world ones show.
        for (auto [e, item, t] : r.view<Item, Transform>().each()) {
            glm::vec2 pixel;
            if (!worldToScreen(frame, t.position + KR::vec3{0, 0.5f, 0}, pixel)) {
                continue;
            }
            const ImVec2 size = ImGui::CalcTextSize(item.name.c_str());
            const ImVec2 min{pixel.x - size.x * 0.5f, pixel.y - size.y * 0.5f};
            bg->AddRectFilled({min.x - pad.x, min.y - pad.y},
                              {min.x + size.x + pad.x, min.y + size.y + pad.y},
                              IM_COL32(0, 0, 0, 255));
            bg->AddText(min, IM_COL32(255, 255, 255, 255), item.name.c_str());
        }
        ImGui::PopFont();
    }
};
