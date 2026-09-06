#include "entt/entity/fwd.hpp"
#include "glm/glm.hpp"
#include "../../lib/plugin/plugin.hpp"


struct Transform {
	glm::vec3 position = {0.0f, 0.0f, 0.0f};
	glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
	glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
	glm::mat4 matrix() const;
};
struct GlobalTransform: public Transform{};
struct LocalTransform: public Transform{};


struct TransformPlugin : public Plugin {
    void init(entt::registry &r) override {
        r.ctx().emplace<GlobalTransform>();
        r.ctx().emplace<LocalTransform>();
    };
    void update(entt::registry &r) override {
    };
};
