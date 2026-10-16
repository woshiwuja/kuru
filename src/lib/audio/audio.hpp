#pragma once
#include "../settings/settings.hpp"
#include <fmod_studio.hpp>
#include <string>
#include <vector>

namespace KR {
// A playing FMOD Studio event. Empty (instance == nullptr) when play() failed.
struct AudioTrack {
  FMOD::Studio::EventInstance *instance = nullptr;
};

struct AudioManager {
  void init();
  void close();
  // Once per frame: FMOD does its mixing/streaming bookkeeping here.
  void update(const Settings::Audio &settings);
  // .bank files from FMOD Studio; "Master.bank" + "Master.strings.bank" first.
  void loadBank(const std::string &path);
  // eventPath as in FMOD Studio, e.g. "event:/Music/Theme".
  AudioTrack play(const std::string &eventPath);
  void pause(const AudioTrack &track, bool paused);
  void stop(const AudioTrack &track);

private:
  FMOD::Studio::System *system = nullptr;
  std::vector<FMOD::Studio::Bank *> banks;
};
inline bool isTrackValid(const AudioTrack &track){
    return track.instance != nullptr && track.instance->isValid();
}
inline bool isTrackValid(AudioTrack &track){
    return track.instance != nullptr && track.instance->isValid();
}
} // namespace KR
