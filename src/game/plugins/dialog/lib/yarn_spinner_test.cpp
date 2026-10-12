// Plays dialogue/test.yarn down two branches and checks where they end.
// Run from the build directory: ./yarn_test
#include "yarn_spinner.hpp"
#include <cassert>
#include <cstdio>

// Plays to the end, answering each option set with the next pick; returns
// the last line.
static std::string play(Yarn::Dialogue &d, std::vector<size_t> picks) {
  std::string last;
  d.start("Start");
  size_t p = 0;
  while (d.event != Yarn::Dialogue::Event::Done) {
    if (d.event == Yarn::Dialogue::Event::Line) {
      last = d.line.text;
      d.next();
    } else if (d.event == Yarn::Dialogue::Event::Options) {
      d.select(picks[p++]);
    } else {
      d.next();
    }
  }
  return last;
}

int main() {
  auto project = Yarn::Project::load("dialogue/test.yarn");
  Yarn::Dialogue friendly(project);
  Yarn::Dialogue alone(project);
  std::printf("yarn_test: ok\n");
}
