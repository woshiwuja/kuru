#include "settings.hpp"
#include <fstream>
#include <iostream>
#include <json.hpp> // nlohmann, vendored by tinygltf

namespace KR {
namespace {
using json = nlohmann::ordered_json; // save() keeps the file's key order

// Only overwrites `out` when the key is there, so absent keys keep defaults.
template <typename T> void read(const json &j, const char *key, T &out) {
  if (j.contains(key)) {
    j.at(key).get_to(out);
  }
}

void readQuality(const json &j, Quality &out) {
  if (!j.contains("quality")) {
    return;
  }
  const std::string q = j.at("quality").get<std::string>();
  if (q == "off") {
    out = off;
  } else if (q == "low") {
    out = low;
  } else if (q == "medium") {
    out = medium;
  } else if (q == "high") {
    out = high;
  } else {
    std::cerr << "settings: unknown quality \"" << q << "\", keeping default\n";
  }
}

const char *qualityName(Quality q) {
  if (q == off) {
    return "off";
  }
  if (q == low) {
    return "low";
  }
  if (q == high) {
    return "high";
  }
  return "medium";
}

const json &child(const json &j, const char *key) {
  static const json empty = json::object();
  if (j.contains(key)) {
    return j.at(key);
  }
  return empty;
}
} // namespace

void Settings::load(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "settings: " << path << " not found, using defaults\n";
    return;
  }
  try {
    const json j = json::parse(file);

    const json &win = child(child(j, "video"), "window");
    int mode = video.window.mode;
    read(win, "mode", mode);
    video.window.mode = static_cast<WindowMode>(mode);
    read(win, "width", video.window.width);
    read(win, "height", video.window.height);

    const json &gfx = child(child(j, "video"), "graphics");
    readQuality(child(gfx, "textures"), video.graphics.textures.quality);
    readQuality(child(gfx, "shadows"), video.graphics.shadows.quality);

    const json &a = child(j, "audio");
    read(a, "master_volume", audio.master_volume);
    read(a, "sound_effects_volume", audio.sound_effects_volume);
    read(a, "music_volume", audio.music_volume);
    read(a, "output_device", audio.output_device);
  } catch (const json::exception &e) {
    std::cerr << "settings: " << path << ": " << e.what() << "\n";
  }
}

void Settings::save(const std::string &path) const {
  const json j = {
      {"general", json::object()},
      {"gameplay", json::object()},
      {"video",
       {{"window",
         {{"mode", static_cast<int>(video.window.mode)},
          {"width", video.window.width},
          {"height", video.window.height}}},
        {"graphics",
         {{"textures", {{"quality", qualityName(video.graphics.textures.quality)}}},
          {"shadows", {{"quality", qualityName(video.graphics.shadows.quality)}}}}}}},
      {"audio",
       {{"master_volume", audio.master_volume},
        {"sound_effects_volume", audio.sound_effects_volume},
        {"music_volume", audio.music_volume},
        {"output_device", audio.output_device}}}};
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    std::cerr << "settings: could not open " << path << " for writing\n";
    return;
  }
  file << j.dump(4) << "\n";
}
} // namespace KR
