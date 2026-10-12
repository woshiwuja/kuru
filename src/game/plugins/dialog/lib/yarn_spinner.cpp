#include "yarn_spinner.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace Yarn {

std::string toString(const Value &v) {
  if (const float *f = std::get_if<float>(&v)) {
    return std::format("{}", *f); // shortest form: "1", "1.5"
  }
  if (const bool *b = std::get_if<bool>(&v)) {
    if (*b) {
      return "True";
    }
    return "False";
  }
  return std::get<std::string>(v);
}

float toNumber(const Value &v) {
  if (const float *f = std::get_if<float>(&v)) {
    return *f;
  }
  if (const bool *b = std::get_if<bool>(&v)) {
    if (*b) {
      return 1.0f;
    }
    return 0.0f;
  }
  return std::stof(std::get<std::string>(v));
}

bool toBool(const Value &v) {
  if (const bool *b = std::get_if<bool>(&v)) {
    return *b;
  }
  if (const float *f = std::get_if<float>(&v)) {
    return *f != 0.0f;
  }
  std::string s = std::get<std::string>(v);
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  if (s == "true") {
    return true;
  }
  if (s == "false") {
    return false;
  }
  throw std::runtime_error("yarn: can't convert \"" + s + "\" to bool");
}

// RFC 4180 rows: quoted fields may hold commas, "" and newlines.
static std::vector<std::vector<std::string>> parseCsv(const std::string &text) {
  std::vector<std::vector<std::string>> rows(1);
  std::string field;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    if (quoted) {
      if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
        field += '"';
        i++;
      } else if (c == '"') {
        quoted = false;
      } else {
        field += c;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      rows.back().push_back(std::move(field));
      field.clear();
    } else if (c == '\n') {
      rows.back().push_back(std::move(field));
      field.clear();
      rows.emplace_back();
    } else if (c != '\r') {
      field += c;
    }
  }
  if (!field.empty() || !rows.back().empty()) {
    rows.back().push_back(std::move(field));
  }
  return rows;
}

static std::string readFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw std::runtime_error("yarn: can't open " + path);
  }
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

std::shared_ptr<const Project> Project::load(const std::string &yarnPath) {
  std::string base = yarnPath;
  if (const size_t dot = base.rfind('.'); dot != std::string::npos) {
    base.resize(dot);
  }
  auto project = std::make_shared<Project>();
  if (!project->program.ParseFromString(readFile(base + ".yarnc"))) {
    throw std::runtime_error("yarn: " + base + ".yarnc is not a compiled program");
  }
  // id,text,file,node,lineNumber
  for (auto &row : parseCsv(readFile(base + "-Lines.csv"))) {
    if (row.size() >= 2) {
      project->lines[row[0]] = row[1];
    }
  }
  return project;
}

void Dialogue::start(const std::string &nodeName) {
  stack.clear();
  callStack.clear();
  pending.clear();
  salient.clear();
  setNode(nodeName);
  next();
}

void Dialogue::next() {
  options.clear();
  event = Event::Done;
  running = node != nullptr;
  while (running) {
    run(node->instructions(pc));
    pc++;
    if (running && pc >= node->instructions_size()) {
      returnFromNode(node);
      node = nullptr;
      running = false;
    }
  }
}

void Dialogue::select(size_t option) {
  if (event != Event::Options || option >= pending.size()) {
    throw std::runtime_error("yarn: no option " + std::to_string(option) + " to select");
  }
  stack.push_back(float(pending[option].destination));
  stack.push_back(true); // an option was picked
  pending.clear();
  next();
}

Value Dialogue::variable(const std::string &name) const {
  if (auto it = variables.find(name); it != variables.end()) {
    return it->second;
  }
  const auto &initial = project->program.initial_values();
  auto it = initial.find(name);
  if (it == initial.end()) {
    throw std::runtime_error("yarn: variable " + name + " has no value");
  }
  if (it->second.has_string_value()) {
    return it->second.string_value();
  }
  if (it->second.has_bool_value()) {
    return it->second.bool_value();
  }
  return it->second.float_value();
}

Value Dialogue::pop() {
  if (stack.empty()) {
    throw std::runtime_error("Popped Yarn Value from empty stack");
  }
  Value v = std::move(stack.back());
  stack.pop_back();
  return v;
}

const Value &Dialogue::peek() const {
  if (stack.empty()) {
    throw std::runtime_error("yarn: stack underflow");
  }
  return stack.back();
}

void Dialogue::setNode(const std::string &name) {
  auto it = project->program.nodes().find(name);
  if (it == project->program.nodes().end()) {
    throw std::runtime_error("yarn: no node named " + name);
  }
  node = &it->second;
  pc = 0;
}

// The instruction after this one is the node's first: run() returns, and
// next() increments pc from -1 to 0.
void Dialogue::jumpToNode(std::string name, bool detour) {
  if (name.empty()) {
    name = toString(peek());
  }
  if (detour) {
    callStack.push_back({node->name(), pc});
  } else {
    returnFromNode(node);
    while (!callStack.empty()) {
      returnFromNode(&project->program.nodes().at(callStack.back().node));
      callStack.pop_back();
    }
    stack.clear();
  }
  setNode(name);
  pc = -1;
}

static const std::string trackingHeader = "$Yarn.Internal.TrackingVariable";

static std::string trackingVariable(const Yarn::Node &n) {
  for (const auto &h : n.headers()) {
    if (h.key() == trackingHeader) {
      return h.value();
    }
  }
  return "";
}

// Counts the visit, if the script asks for visited()/visited_count() on it.
void Dialogue::returnFromNode(const Yarn::Node *n) {
  if (n == nullptr) {
    return;
  }
  const std::string var = trackingVariable(*n);
  if (!var.empty()) {
    variables[var] = toNumber(variable(var)) + 1.0f;
  }
}

float Dialogue::visitCount(const std::string &nodeName) const {
  auto it = project->program.nodes().find(nodeName);
  if (it == project->program.nodes().end()) {
    return 0.0f;
  }
  const std::string var = trackingVariable(it->second);
  if (var.empty()) {
    return 0.0f;
  }
  return toNumber(variable(var));
}

void Dialogue::stop() {
  returnFromNode(node);
  while (!callStack.empty()) {
    returnFromNode(&project->program.nodes().at(callStack.back().node));
    callStack.pop_back();
  }
  node = nullptr;
  running = false;
}

// Pops `substitutions` values into the line's {0}, {1}, ... markers.
Line Dialogue::makeLine(const std::string &id, int substitutions) {
  std::vector<std::string> subs(substitutions);
  for (int i = substitutions - 1; i >= 0; i--) {
    subs[i] = toString(pop());
  }
  Line l{.id = id};
  if (auto it = project->lines.find(id); it != project->lines.end()) {
    l.text = it->second;
  } else {
    l.text = id;
  }
  for (int i = 0; i < substitutions; i++) {
    const std::string marker = "{" + std::to_string(i) + "}";
    for (size_t at = l.text.find(marker); at != std::string::npos;
         at = l.text.find(marker, at + subs[i].size())) {
      l.text.replace(at, marker.size(), subs[i]);
    }
  }
  // ponytail: only the "Name: " prefix is parsed; [markup] stays in the text
  if (const size_t colon = l.text.find(": "); colon != std::string::npos &&
                                               l.text.find(' ') > colon) {
    l.character = l.text.substr(0, colon);
    l.text = l.text.substr(colon + 2);
  }
  return l;
}

void Dialogue::run(const Yarn::Instruction &i) {
  using I = Yarn::Instruction;
  switch (i.InstructionType_case()) {
  case I::kJumpTo:
    pc = i.jumpto().destination() - 1;
    break;
  case I::kPeekAndJump:
    pc = int(toNumber(peek())) - 1;
    break;
  case I::kRunLine:
    line = makeLine(i.runline().lineid(), i.runline().substitutioncount());
    event = Event::Line;
    running = false;
    break;
  case I::kRunCommand: {
    command = i.runcommand().commandtext();
    for (int n = i.runcommand().substitutioncount() - 1; n >= 0; n--) {
      const std::string sub = toString(pop());
      const std::string marker = "{" + std::to_string(n) + "}";
      if (const size_t at = command.rfind(marker); at != std::string::npos) {
        command.replace(at, marker.size(), sub);
      }
    }
    event = Event::Command;
    running = false;
    break;
  }
  case I::kAddOption: {
    const auto &o = i.addoption();
    Line l = makeLine(o.lineid(), o.substitutioncount());
    bool enabled = true;
    if (o.hascondition()) {
      enabled = toBool(pop());
    }
    pending.push_back({std::move(l), o.destination(), enabled});
    break;
  }
  case I::kShowOptions:
    if (pending.empty()) {
      stop();
      break;
    }
    for (const auto &p : pending) {
      options.push_back({p.line, p.enabled});
    }
    event = Event::Options;
    running = false;
    break;
  case I::kPushString:
    stack.push_back(i.pushstring().value());
    break;
  case I::kPushFloat:
    stack.push_back(i.pushfloat().value());
    break;
  case I::kPushBool:
    stack.push_back(i.pushbool().value());
    break;
  case I::kJumpIfFalse:
    if (!toBool(peek())) {
      pc = i.jumpiffalse().destination() - 1;
    }
    break;
  case I::kPop:
    pop();
    break;
  case I::kCallFunc:
    callFunction(i.callfunc().functionname());
    break;
  case I::kPushVariable:
    stack.push_back(variable(i.pushvariable().variablename()));
    break;
  case I::kStoreVariable:
    variables[i.storevariable().variablename()] = peek();
    break;
  case I::kStop:
    stop();
    break;
  case I::kRunNode:
    jumpToNode(i.runnode().nodename(), false);
    break;
  case I::kPeekAndRunNode:
    jumpToNode("", false);
    break;
  case I::kDetourToNode:
    jumpToNode(i.detourtonode().nodename(), true);
    break;
  case I::kPeekAndDetourToNode:
    jumpToNode("", true);
    break;
  case I::kReturn: {
    returnFromNode(node);
    if (callStack.empty()) {
      node = nullptr;
      running = false;
      break;
    }
    CallSite site = callStack.back();
    callStack.pop_back();
    setNode(site.node);
    pc = site.pc;
    break;
  }
  case I::kAddSaliencyCandidate: {
    const bool passed = toBool(pop());
    salient.push_back({i.addsaliencycandidate().destination(), passed});
    break;
  }
  case I::kSelectSaliencyCandidate: {
    // ponytail: first passing candidate; upstream picks randomly among the
    // best least-recently-seen ones
    auto it = std::find_if(salient.begin(), salient.end(),
                           [](const auto &c) { return c.second; });
    if (it != salient.end()) {
      stack.push_back(float(it->first));
      stack.push_back(true);
    } else {
      stack.push_back(false);
    }
    salient.clear();
    break;
  }
  default:
    // AddSaliencyCandidateFromNode: node groups need smart variables
    throw std::runtime_error("yarn: unsupported instruction " +
                             std::to_string(int(i.InstructionType_case())));
  }
}

static std::mt19937 &rng() {
  static std::mt19937 r{std::random_device{}()};
  return r;
}

static float integer(float v) { return std::trunc(v); }

// Arguments are on the stack under their count; the result goes back on it.
void Dialogue::callFunction(const std::string &name) {
  const int count = int(toNumber(pop()));
  std::vector<Value> a(count);
  for (int n = count - 1; n >= 0; n--) {
    a[n] = pop();
  }
  if (auto it = functions.find(name); it != functions.end()) {
    stack.push_back(it->second(a));
    return;
  }
  // Operators are "<Type>.<Operator>": Number.Add, String.EqualTo, ...
  std::string op = name;
  if (const size_t dot = name.find('.'); dot != std::string::npos) {
    op = name.substr(dot + 1);
  }
  auto num = [&](size_t n) { return toNumber(a.at(n)); };
  auto boolean = [&](size_t n) { return toBool(a.at(n)); };
  Value r;
  if (op == "EqualTo") {
    r = a.at(0) == a.at(1);
  } else if (op == "NotEqualTo") {
    r = a.at(0) != a.at(1);
  } else if (op == "Add") {
    if (std::holds_alternative<std::string>(a.at(0))) {
      r = toString(a[0]) + toString(a.at(1));
    } else {
      r = num(0) + num(1);
    }
  } else if (op == "Minus") {
    r = num(0) - num(1);
  } else if (op == "Multiply") {
    r = num(0) * num(1);
  } else if (op == "Divide") {
    r = num(0) / num(1);
  } else if (op == "Modulo") {
    r = std::fmod(num(0), num(1));
  } else if (op == "UnaryMinus") {
    r = -num(0);
  } else if (op == "GreaterThan") {
    r = num(0) > num(1);
  } else if (op == "GreaterThanOrEqualTo") {
    r = num(0) >= num(1);
  } else if (op == "LessThan") {
    r = num(0) < num(1);
  } else if (op == "LessThanOrEqualTo") {
    r = num(0) <= num(1);
  } else if (op == "And") {
    r = boolean(0) && boolean(1);
  } else if (op == "Or") {
    r = boolean(0) || boolean(1);
  } else if (op == "Xor") {
    r = boolean(0) != boolean(1);
  } else if (op == "Not") {
    r = !boolean(0);
  } else if (name == "string" || name == "format_invariant") {
    r = toString(a.at(0));
  } else if (name == "number") {
    r = num(0);
  } else if (name == "bool") {
    r = boolean(0);
  } else if (name == "visited") {
    r = visitCount(toString(a.at(0))) > 0.0f;
  } else if (name == "visited_count") {
    r = visitCount(toString(a.at(0)));
  } else if (name == "random") {
    r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng());
  } else if (name == "random_range" || name == "random_range_float") {
    r = float(std::uniform_int_distribution<int>(int(num(0)), int(num(1)))(rng()));
  } else if (name == "dice") {
    r = float(std::uniform_int_distribution<int>(1, int(num(0)))(rng()));
  } else if (name == "min") {
    r = std::min(num(0), num(1));
  } else if (name == "max") {
    r = std::max(num(0), num(1));
  } else if (name == "round") {
    r = std::round(num(0));
  } else if (name == "round_places") {
    const float scale = std::pow(10.0f, num(1));
    r = std::round(num(0) * scale) / scale;
  } else if (name == "floor") {
    r = std::floor(num(0));
  } else if (name == "ceil") {
    r = std::ceil(num(0));
  } else if (name == "inc") {
    if (num(0) == integer(num(0))) {
      r = num(0) + 1.0f;
    } else {
      r = std::ceil(num(0));
    }
  } else if (name == "dec") {
    if (num(0) == integer(num(0))) {
      r = num(0) - 1.0f;
    } else {
      r = std::floor(num(0));
    }
  } else if (name == "decimal") {
    r = num(0) - integer(num(0));
  } else if (name == "int") {
    r = integer(num(0));
  } else {
    throw std::runtime_error("yarn: no function named " + name);
  }
  stack.push_back(std::move(r));
}

} // namespace Yarn
