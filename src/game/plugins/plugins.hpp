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

// Loads mesh and texture through their caches and attaches them to `entity`,
// which the caller creates - so a mesh can be added to an entity that already
// carries other components (e.g. a Character), instead of always landing on
// a fresh entity of its own. params.x >= 0.5 asks for procedural terrain
// shading, in which case params.y/z (the world height range) are filled in
// from the mesh, overwriting whatever was passed in; texturePath can be empty
// when the mesh carries its own embedded texture. transform is only applied
// if `entity` doesn't already carry one.
void spawn(entt::registry &reg, entt::entity entity,
           const std::string &meshPath, const std::string &texturePath,
           glm::vec4 params = {0.0f, 0.0f, 1.0f, 0.0f},
           Transform transform = {});
