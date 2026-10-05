#include "plugin.hpp"

namespace KR {
std::vector<SerialEntry> &serialTypes() {
  static std::vector<SerialEntry> v;
  return v;
}
} // namespace KR
