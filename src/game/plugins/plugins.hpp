#pragma once
// Helpers shared by the game plugins. Not a plugin itself, and not a place to
// register them: each plugin is standalone and main() decides which ones run.
#include "../../lib/image/image.hpp"
#include "render.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <string>

// The render plugin, published into the registry context by RenderPlugin::init.
RenderPlugin &renderer(entt::registry &reg);

// Textures are shared between entities, so they go through a cache keyed by
// path, same as meshes. The cache is created on first use.
std::shared_ptr<Texture> getTexture(entt::registry &reg,
                                    const std::string &path);

// Loads mesh and texture through their caches and hands the entity to the
// renderer. For plain models: terrain comes in through HeightmapPlugin.
entt::entity spawn(entt::registry &reg, const std::string &meshPath,
                   const std::string &texturePath,
                   glm::vec4 params = {0.0f, 0.0f, 1.0f, 0.0f},
                   Transform transform = {});
