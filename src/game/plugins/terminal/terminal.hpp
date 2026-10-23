#pragma once
#include "entt/entity/fwd.hpp"
#include "plugin/plugin.hpp"
#include <Kuru.h>
#include <memory>
#include <unordered_map>

// Add to an entity (e.g. from the inspector) to open a shell in an ImGui
// window; closing the window removes it again. A port of ghostling's main.c
// (github.com/ghostty-org/ghostling) from raylib to ImGui.
struct Terminal{};

struct TerminalSettings{};

struct TerminalSession; // terminal.cpp: the pty plus its libghostty-vt state

struct TerminalPlugin : KR::Plugin {
    TerminalPlugin();
    ~TerminalPlugin() override;
    void init(entt::registry &r) override;
    void update(entt::registry &r) override;

    ImFont *font = nullptr;
    std::unordered_map<entt::entity, std::unique_ptr<TerminalSession>> sessions;
};
