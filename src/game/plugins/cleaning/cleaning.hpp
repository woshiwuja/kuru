#pragma once
#include <Kuru.h>
struct PendingClean{};
struct CleaningPlugin : public KR::Plugin{
    void update(entt::registry &r)override;
};
