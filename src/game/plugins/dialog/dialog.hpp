#pragma once
#include "../character/character.hpp"
#include "../character/controller.hpp"
#include "../name/name.hpp"
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

// What this entity says: a .yarn file, run from its "Start" node when a
// player character sent to it (it is made Interactable) gets in talkRange.
// CMake compiles it with ysc next to the binary, as <name>.yarnc and
// <name>-Lines.csv.
struct Talkable {
  std::string path;
};

// On a player character in a conversation: one each, so several can talk at
// once in different places. The window shows the selected one's.
struct Talking {
  entt::entity with = entt::null;
  std::optional<Yarn::Dialogue> dialogue;
  // `dialogue` run one step past its current line, to show the options
  // right after it with it. Kept, not rerun each frame: a random() in an
  // option's condition would flicker.
  std::optional<Yarn::Dialogue> ahead;
  std::string error;
};

struct DialoguePlugin : public Plugin {
  static constexpr float talkRange = 2.0f; // m, in XZ

  DialoguePlugin() { registerComponent<Talkable>(); }

  // Each file is loaded once, however many entities say it.
  std::map<std::string, std::shared_ptr<const Yarn::Project>> projects;

  void update(entt::registry &r) override {
    std::vector<entt::entity> untouched;
    for (auto e : r.view<Talkable>(entt::exclude<Interactable>)) {
      untouched.push_back(e);
    }
    for (entt::entity e : untouched) {
      r.emplace<Interactable>(e);
    }
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
    std::string speaker;
    if (const auto *name = r.try_get<Name>(talk.with)) {
      speaker = name->fname;
    }
    // "###": the title changes with who is talked to, the window stays one.
    std::string title = "Dialogue";
    if (!speaker.empty()) {
      title = speaker;
    }
    title += "###Dialogue";
    bool open = true;
    if (ImGui::Begin(title.c_str(), &open)) {
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
    for (auto [e, to, t] : r.view<InteractWith, Transform>().each()) {
      if (!r.valid(to.with)) {
        done.push_back(e);
        continue;
      }
      if (!r.all_of<Talkable>(to.with)) {
        continue; // someone else's interactable
      }
      const auto *other = r.try_get<Transform>(to.with);
      if (!other) {
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
      r.remove<InteractWith>(e);
    }
    for (auto [e, with] : reached) {
      r.remove<InteractWith, WalkPath>(e);
      auto &talk = r.emplace_or_replace<Talking>(e);
      talk.with = with;
      start(talk, r.get<Talkable>(with).path);
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
      talk.ahead.reset();
      talk.error = ex.what();
    }
  }

  // Buttons for d's options; true once one was picked (d then moved on).
  static bool pickOption(Yarn::Dialogue &d) {
    using namespace ImGui;
    for (size_t i = 0; i < d.options.size(); i++) {
      BeginDisabled(!d.options[i].enabled);
      PushID(int(i));
      const bool picked = Button(d.options[i].line.text.c_str());
      PopID();
      EndDisabled();
      if (picked) {
        d.select(i);
        return true; // options were replaced
      }
    }
    return false;
  }

  // The one talked to is named in the title: lines show no speaker.
  void show(Talking &talk) {
    using namespace ImGui;
    using Event = Yarn::Dialogue::Event;
    auto &d = *talk.dialogue;
    try {
      if (d.event == Event::Line) {
        TextWrapped("%s", d.line.text.c_str());
        if (!talk.ahead) {
          talk.ahead = d;
          talk.ahead->next();
        }
        if (talk.ahead->event == Event::Options) {
          Separator();
          if (pickOption(*talk.ahead)) {
            d = std::move(*talk.ahead);
            talk.ahead.reset();
          }
        } else if (Button("Continue")) {
          d = std::move(*talk.ahead);
          talk.ahead.reset();
        }
      } else if (d.event == Event::Options) {
        pickOption(d);
      } else if (d.event == Event::Command) {
        d.next();
      }
    } catch (const std::exception &ex) {
      talk.dialogue.reset();
      talk.ahead.reset();
      talk.error = ex.what();
    }
  }
};
