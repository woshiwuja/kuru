#pragma once
#include <Recast.h>
#include <vector>
#include "../../lib/plugin/plugin.hpp"
#include "map.hpp"
#include "mesh_registry.hpp"
#include "entt/entity/fwd.hpp"
struct NavSettings{
    rcContext ctx;
    rcConfig cfg;
};
struct NavAgent{
    float height;
    float radius;
    float climb;
};
struct NavigationPlugin : public Plugin{
    void init(entt::registry &reg)override{
        for (auto [e, settings, mesh]: reg.view<Map, NavSettings, Mesh>().each()){
            auto navAgent = reg.emplace<NavAgent>(e,NavAgent{.height = 1, .radius = 1, .climb =1});
            settings.cfg.cs = 0.3f;   // cell size, mondo
            settings.cfg.ch = 0.2f;   // cell height, mondo
            settings.cfg.walkableHeight = (int)ceilf(navAgent.height / settings.cfg.ch);   // voxel
            settings.cfg.walkableRadius = (int)ceilf(navAgent.radius / settings.cfg.cs);   // voxel
            settings.cfg.walkableClimb  = (int)floorf(navAgent.climb / settings.cfg.ch);   // voxel, floor non ceil
            settings.cfg.maxEdgeLen     = (int)(12.0f / settings.cfg.cs);               // voxel
            settings.cfg.walkableSlopeAngle = 45.0f;    // gradi, non voxel
            settings.cfg.detailSampleDist   = 6.0f * settings.cfg.cs;   // mondo
            settings.cfg.detailSampleMaxError = 1.0f * settings.cfg.ch; // mondo
        }
    }
};
