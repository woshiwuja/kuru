#pragma once
#include "../character/character.hpp"
#include "../character/controller.hpp"
#include "../transform/transform.hpp"
#include "lib/yarn_spinner.hpp"
#include <Kuru.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using namespace KR;

// What this entity says, and what makes it talkable: a .yarn file, run from
// its "Start" node when a player character right-clicked onto it gets in range.
// CMake compiles it with ysc next to the binary, as <name>.yarnc and
// <name>-Lines.csv.
struct DialoguePath {
  std::string path;
};

// On a player character walking to `with` to talk to it (right-click on a
// talkable). Dropped if the walk ends out of talkRange.
struct TalkTo {
  entt::entity with = entt::null;
};

// On a player character in a conversation: one each, so several can talk at
// once in different places. The window shows the selected one's.
struct Talking {
  entt::entity with = entt::null;
  std::optional<Yarn::Dialogue> dialogue;
  std::string error;
};

struct DialoguePlugin : public Plugin {
  static constexpr float talkRange = 2.0f; // m, in XZ

  DialoguePlugin() { registerComponent<DialoguePath>(); }

  // Each file is loaded once, however many entities say it.
  std::map<std::string, std::shared_ptr<const Yarn::Project>> projects;

  void update(entt::registry &r) override {
    arrive(r);

    entt::entity shown = entt::null;
    for (auto e : r.view<Selected, PlayerCharacter, Talking>()) {
      shown = e;
      break;
    }
    if (shown == entt::null) {
      return;
    }
    auto &talk = r.get<Talking>(shown);
    bool open = true;
    if (ImGui::Begin("Dialogue", &open)) {
      if (!talk.error.empty()) {
        ImGui::TextWrapped("%s", talk.error.c_str());
      } else {
        show(talk);
      }
    }
    ImGui::End();
    if (!open || (talk.dialogue && talk.dialogue->event == Yarn::Dialogue::Event::Done)) {
      r.remove<Talking>(shown);
    }
  }

  // Main characters walking to someone: in range, they stop and talk.
  void arrive(entt::registry &r) {
    std::vector<entt::entity> done;
    std::vector<std::pair<entt::entity, entt::entity>> reached;
    for (auto [e, to, t] : r.view<TalkTo, Transform>().each()) {
      const auto *other = r.valid(to.with) ? r.try_get<Transform>(to.with) : nullptr;
      if (!other || !r.all_of<DialoguePath>(to.with)) {
        done.push_back(e);
        continue;
      }
      const glm::vec2 gap{other->position.x - t.position.x,
                          other->position.z - t.position.z};
      if (glm::length(gap) <= talkRange) {
        reached.push_back({e, to.with});
      } else if (!r.all_of<WalkPath>(e)) {
        done.push_back(e); // stopped short: no path, or blocked
      }
    }
    for (entt::entity e : done) {
      r.remove<TalkTo>(e);
    }
    for (auto [e, with] : reached) {
      r.remove<TalkTo, WalkPath>(e);
      auto &talk = r.emplace_or_replace<Talking>(e);
      talk.with = with;
      start(talk, r.get<DialoguePath>(with).path);
    }
  }

  void start(Talking &talk, const std::string &path) {
    try {
      auto &project = projects[path];
      if (!project) {
        project = Yarn::Project::load(assetPath(path));
      }
      talk.dialogue.emplace(project);
      talk.dialogue->start("Start");
    } catch (const std::exception &ex) {
      talk.dialogue.reset();
      talk.error = ex.what();
    }
  }

  void show(Talking &talk) {
    using namespace ImGui;
    using Event = Yarn::Dialogue::Event;
    auto &d = *talk.dialogue;
    try {
      if (d.event == Event::Line) {
        if (!d.line.character.empty()) {
          TextColored({1.0f, 0.8f, 0.4f, 1.0f}, "%s", d.line.character.c_str());
        }
        TextWrapped("%s", d.line.text.c_str());
        if (Button("Continue")) {
          d.next();
        }
      } else if (d.event == Event::Options) {
        for (size_t i = 0; i < d.options.size(); i++) {
          BeginDisabled(!d.options[i].enabled);
          PushID(int(i));
          const bool picked = Button(d.options[i].line.text.c_str());
          PopID();
          EndDisabled();
          if (picked) {
            d.select(i);
            break; // options were replaced
          }
        }
      } else if (d.event == Event::Command) {
        d.next();
      }
    } catch (const std::exception &ex) {
      talk.dialogue.reset();
      talk.error = ex.what();
    }
  }
};
