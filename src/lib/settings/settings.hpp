#pragma once
#include <cstdint>
#include <string>
#include "../window/window.hpp"

namespace KR {
enum Quality { off = 0, low = 1, medium = 2, high = 3 };

struct Settings {
  struct General {
  } general;

  struct Gameplay {
  } gameplay;

  struct Video {
    Window window{.width = 1920, .height = 1080}; 
    struct Graphics {
      struct Textures {
        Quality quality = medium;
      } textures;
      struct Shadows {
        Quality quality = medium;
      } shadows;
      bool vsync = true;
    } graphics;
  } video;

  struct Audio {
    uint32_t master_volume = 100; 
    uint32_t sound_effects_volume = 100;
    uint32_t music_volume = 100;
    std::string output_device; 
  } audio;
  void load(const std::string &path);
  void save(const std::string &path) const;
};
} // namespace KR
