#pragma once
// Yarn Spinner 3 runtime: runs the .yarnc programs ysc compiles (a
// Yarn::Program protobuf, see yarn_spinner.proto) with the -Lines.csv string
// table next to it. A port of YarnSpinner's VirtualMachine.cs; the Unreal
// plugin's C++ VM runs the older v2 bytecode, so it couldn't be reused as is.
#include "yarn_spinner.pb.h"
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Yarn {

using Value = std::variant<float, std::string, bool>;
std::string toString(const Value &v);
float toNumber(const Value &v);
bool toBool(const Value &v);

// A compiled .yarn file: load("dialogue/test.yarn") reads
// dialogue/test.yarnc and dialogue/test-Lines.csv.
struct Project {
  Yarn::Program program;
  std::unordered_map<std::string, std::string> lines; // line ID -> text
  static std::shared_ptr<const Project> load(const std::string &yarnPath);
};

struct Line {
  std::string id;
  std::string character; // "Traveler" in "Traveler: Hi", else empty
  std::string text;      // substitutions applied, character name cut off
};

struct Option {
  Line line;
  bool enabled = true; // false: its <<if>> failed; show greyed, or hide
};

// One conversation. Poll it: after start()/next()/select() it has stopped on
// a line (wait for the player, then next()), options (select()), a command
// (run it, then next()) or the end.
struct Dialogue {
  enum class Event { Done, Line, Options, Command };

  std::shared_ptr<const Project> project;
  // ponytail: per dialogue; share one map between dialogues for world state
  std::unordered_map<std::string, Value> variables;
  // Your own functions, by name. Built-ins (operators, visited, random, ...)
  // are handled when a name isn't here.
  std::unordered_map<std::string, std::function<Value(std::span<const Value>)>>
      functions;

  Event event = Event::Done;
  Line line;
  std::vector<Option> options;
  std::string command;

  explicit Dialogue(std::shared_ptr<const Project> p) : project(std::move(p)) {}

  void start(const std::string &node);
  void next();
  void select(size_t option);
  Value variable(const std::string &name) const;

private:
  struct PendingOption {
    Line line;
    int destination;
    bool enabled;
  };
  struct CallSite {
    std::string node;
    int pc;
  };

  const Yarn::Node *node = nullptr;
  int pc = 0;
  bool running = false;
  std::vector<Value> stack;
  std::vector<CallSite> callStack;
  std::vector<PendingOption> pending;
  // Line group candidates: (destination, condition passed)
  std::vector<std::pair<int, bool>> salient;

  Value pop();
  const Value &peek() const;
  void setNode(const std::string &name);
  void jumpToNode(std::string name, bool detour);
  void returnFromNode(const Yarn::Node *n);
  void stop();
  Line makeLine(const std::string &id, int substitutions);
  void run(const Yarn::Instruction &i);
  void callFunction(const std::string &name);
  float visitCount(const std::string &node) const;
};

} // namespace Yarn
