// Sound effects.
//
// The original played the 138 WAV samples embedded in INIT.PH through the
// Miles Sound System (WAIL32.DLL: AIL_allocate_sample_handle / AIL_start_sample,
// see FUN_0043ce10 and FUN_0043cc10) and streamed music from CD audio tracks via
// MCI ("cdaudio").  Here the samples are decoded once and mixed in software;
// the platform layer pulls 16-bit stereo PCM from Mixer::render().
#pragma once
#include "ph/level.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace engine {

struct DecodedSample {
    int id = 0;
    int sampleRate = 11025;
    std::vector<int16_t> mono;      // signed 16-bit mono
};

class Mixer {
public:
    static constexpr int kOutputRate = 44100;
    static constexpr int kVoices = 8;      // AIL sample handles allocated by the original

    void loadBank(const ph::AssetBank& bank);
    // FUN_0043cc10(id, priority): start a sample; volume 0..0x7f as Option_WavVolume.
    void play(int soundId, int volume = 0x7f, bool loop = false);
    void stopAll();
    bool playing(int soundId) const;       // FUN_0043cbc0-style channel test, by sample id
    void stop(int soundId);
    // The game logic's sample channels (FUN_0043cc10(id, channel) / FUN_0043cbc0(channel)):
    // a channel plays one sample at a time and a new one cuts the old; 0x8000 / 0x89 stop it.
    static constexpr int kChannels = 2;
    void playChannel(int channel, int soundId, int volume = 0x7f);
    bool channelBusy(int channel) const;
    void setMasterVolume(int v0to127) { master_ = v0to127; }
    bool enabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; if (!e) stopAll(); }

    // Mix `frames` stereo frames into `out` (interleaved L/R).
    void render(int16_t* out, int frames);
    // Without an audio device (tests, tools) the voices move on with the game's clock instead:
    // the front ends set the device clock once their output runs.
    void advance(int frames);
    void setDeviceClock(bool on) { deviceClock_ = on; }
    bool deviceClock() const { return deviceClock_; }

    const DecodedSample* sample(int id) const;

private:
    struct Voice {
        const DecodedSample* s = nullptr;
        double pos = 0;
        double step = 1;
        int volume = 0x7f;
        bool loop = false;
        bool active = false;
    };
    std::vector<DecodedSample> samples_;
    Voice voices_[kVoices];
    mutable std::mutex mutex_;
    int master_ = 0x50;
    bool enabled_ = true;
    bool deviceClock_ = false;
};

// Minimal RIFF/WAVE PCM decoder (8/16-bit, mono/stereo) used by the mixer.
bool decodeWav(const std::vector<uint8_t>& riff, DecodedSample& out);

// CD audio track numbers per level (tables at 0x470470 = music, 0x4704d8 = ambient).
int levelMusicTrack(int level);
int levelAmbientTrack(int level);

} // namespace engine
