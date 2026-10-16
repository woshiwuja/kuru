#include "audio.hpp"
#include <fmod_errors.h>
#include <iostream>
#include <stdexcept>


namespace KR {

void AudioManager::init() {
    auto res = FMOD::Studio::System::create(&system);
    system->initialize(512, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL,
                                   nullptr);
}

void AudioManager::close() {
  if (system == nullptr) {
    return;
  }
  system->release();
  system = nullptr;
  banks.clear();
}

void AudioManager::update(const Settings::Audio &settings) {
  FMOD::System *core = nullptr;
  FMOD::ChannelGroup *master = nullptr;
  if (system->getCoreSystem(&core) == FMOD_OK &&
      core->getMasterChannelGroup(&master) == FMOD_OK) {
    master->setVolume(settings.master_volume / 100.0f);
  }
  system->update();
}

void AudioManager::loadBank(const std::string &path) {
  FMOD::Studio::Bank *bank = nullptr;
  system->loadBankFile(path.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
  banks.push_back(bank);
}

AudioTrack AudioManager::play(const std::string &eventPath) {
  AudioTrack track;
  FMOD::Studio::EventDescription *desc = nullptr;
  FMOD_RESULT result = system->getEvent(eventPath.c_str(), &desc);
  if (result == FMOD_OK) {
    result = desc->createInstance(&track.instance);
  }
  if (result == FMOD_OK) {
    track.instance->start();
    track.instance->release();
  } else {
    std::cerr << "fmod: " << eventPath << ": " << FMOD_ErrorString(result)
              << "\n";
    track.instance = nullptr;
  }
  return track;
}

void AudioManager::pause(const AudioTrack &track, bool paused) {
  if (isTrackValid(track)) {
    track.instance->setPaused(paused);
  }
}

void AudioManager::stop(const AudioTrack &track) {
  if (isTrackValid(track)) {
    track.instance->stop(FMOD_STUDIO_STOP_ALLOWFADEOUT);
  }
}

} // namespace KR
