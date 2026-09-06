#include "SDL3/SDL_audio.h"
struct Audio {
  SDL_AudioDeviceID dev;
  void init() {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = SDL_AUDIO_S16;
    want.channels = 2;
    //dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  };
};
