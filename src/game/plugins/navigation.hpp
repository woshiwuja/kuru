#include <Recast.h>
#include <vector>
#include "../../lib/plugin/plugin.hpp"
#include "plugins.hpp"
#include "entt/entity/fwd.hpp"
struct NavMesh{
    std::vector<float> verts;
    std::vector<float> tris;
    rcContext ctx;
    rcConfig cfg;
};
struct NavigationPlugin : public Plugin{
    void init(entt::registry &reg)override{

    }
};
