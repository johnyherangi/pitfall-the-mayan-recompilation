#include "engine/audio.h"

#include "ph/ph_stream.h"

#include <algorithm>
#include <cstring>

namespace engine {

bool decodeWav(const std::vector<uint8_t>& riff, DecodedSample& out) {
    if (riff.size() < 12 || std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(riff.data() + 8, "WAVE", 4) != 0)
        return false;
    int channels = 1, bits = 8, rate = 11025;
    size_t p = 12;
    bool gotFmt = false;
    while (p + 8 <= riff.size()) {
        const uint32_t sz = ph::rd32(&riff[p + 4]);
        const uint8_t* body = &riff[p + 8];
        if (p + 8 + sz > riff.size()) break;
        if (std::memcmp(&riff[p], "fmt ", 4) == 0 && sz >= 16) {
            channels = ph::rd16(body + 2);
            rate = int(ph::rd32(body + 4));
            bits = ph::rd16(body + 14);
            gotFmt = true;
        } else if (std::memcmp(&riff[p], "data", 4) == 0 && gotFmt) {
            const int bytesPer = bits / 8;
            const size_t frames = sz / size_t(bytesPer * channels);
            out.sampleRate = rate;
            out.mono.resize(frames);
            for (size_t i = 0; i < frames; ++i) {
                int acc = 0;
                for (int c = 0; c < channels; ++c) {
                    const uint8_t* s = body + (i * channels + c) * bytesPer;
                    acc += (bits == 8) ? (int(s[0]) - 128) * 256 : int(int16_t(ph::rd16(s)));
                }
                out.mono[i] = int16_t(acc / channels);
            }
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

void Mixer::loadBank(const ph::AssetBank& bank) {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    for (const auto& snd : bank.sounds) {
        DecodedSample s;
        s.id = snd.id;
        if (decodeWav(snd.riff, s)) samples_.push_back(std::move(s));
    }
}

const DecodedSample* Mixer::sample(int id) const {
    for (const auto& s : samples_) if (s.id == id) return &s;
    return nullptr;
}

void Mixer::play(int soundId, int volume, bool loop) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const DecodedSample* s = sample(soundId);
    if (!s || s->mono.empty()) return;
    Voice* v = nullptr;
    for (int i = 0; i < kVoices - kChannels; ++i) if (!voices_[i].active) { v = &voices_[i]; break; }
    if (!v) v = &voices_[0];               // steal the oldest-allocated voice (the last two are the channels)
    v->s = s; v->pos = 0; v->step = double(s->sampleRate) / kOutputRate;
    v->volume = std::clamp(volume, 0, 0x7f); v->loop = loop; v->active = true;
}

void Mixer::playChannel(int channel, int soundId, int volume) {
    if (!enabled_ || channel < 0 || channel >= kChannels) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Voice& v = voices_[kVoices - kChannels + channel];
    v.active = false;                      // AIL_end_sample
    if (soundId == 0x8000 || soundId == 0x89) return;
    const DecodedSample* s = sample(soundId);
    if (!s || s->mono.empty()) return;
    v.s = s; v.pos = 0; v.step = double(s->sampleRate) / kOutputRate;
    v.volume = std::clamp(volume, 0, 0x7f); v.loop = false; v.active = true;
}

bool Mixer::channelBusy(int channel) const {
    // FUN_0043cbc0: true while the channel's sample plays (and when there is no sound at all).
    if (!enabled_ || channel < 0 || channel >= kChannels) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    return voices_[kVoices - kChannels + channel].active;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) v.active = false;
}

bool Mixer::playing(int soundId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& v : voices_) if (v.active && v.s && v.s->id == soundId) return true;
    return false;
}

void Mixer::stop(int soundId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) if (v.active && v.s && v.s->id == soundId) v.active = false;
}

void Mixer::render(int16_t* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < frames; ++i) {
        int acc = 0;
        for (auto& v : voices_) {
            if (!v.active) continue;
            const size_t idx = size_t(v.pos);
            if (idx >= v.s->mono.size()) {
                if (v.loop) v.pos = 0; else { v.active = false; continue; }
            }
            acc += v.s->mono[size_t(v.pos)] * v.volume / 0x7f;
            v.pos += v.step;
        }
        acc = acc * master_ / 0x7f;
        const int16_t s = int16_t(std::clamp(acc, -32768, 32767));
        out[i * 2] = s; out[i * 2 + 1] = s;
    }
}

int levelMusicTrack(int level) {
    static const int t[25] = {5, 7, 8, 9, 11, 12, 13, 16, 14, 15, 19, 21, 21, 21, 21, 3, 14, 15, 17, 4, 0, 2, 22, 5, 23};
    return (level >= 0 && level < 25) ? t[level] : 0;
}

int levelAmbientTrack(int level) {
    static const int t[25] = {4, 20, 21, 21, 21, 4, 20, 21, 21, 21, 21, 21, 21, 21, 21, 3, 21, 21, 20, 4, 0, 2, 22, 5, 23};
    return (level >= 0 && level < 25) ? t[level] : 0;
}

} // namespace engine
