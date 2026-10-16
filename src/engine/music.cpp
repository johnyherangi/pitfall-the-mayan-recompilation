#include "engine/music.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb/stb_vorbis.c"   // the declarations (the decoder is in stb_vorbis_impl.cpp)

namespace engine {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A directory entry of `base` whose name is `name` in any case.
fs::path childCI(const fs::path& base, const std::string& name) {
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return {};
    for (const auto& e : fs::directory_iterator(base, ec))
        if (lower(e.path().filename().string()) == name) return e.path();
    return {};
}

// The track a file name stands for: "Track02", "track 2", "Track_02", "02 - name", "02".
int trackOf(const std::string& stem) {
    const std::string s = lower(stem);
    size_t p = s.find("track");
    if (p != std::string::npos) {
        p += 5;
        while (p < s.size() && (s[p] == ' ' || s[p] == '_' || s[p] == '-' || s[p] == '.')) ++p;
    } else {
        p = 0;
    }
    if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) return -1;
    int n = 0;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p])) && n < 1000) n = n * 10 + (s[p++] - '0');
    return n;
}

// Interleaved samples of `channels` channels at `rate` to 44.1 kHz stereo.
void toCdFormat(const std::vector<int32_t>& in, int channels, int rate, std::vector<int16_t>& out) {
    const size_t frames = channels > 0 ? in.size() / size_t(channels) : 0;
    auto at = [&](size_t f, int c) { return in[f * size_t(channels) + size_t(std::min(c, channels - 1))]; };
    if (rate == 44100) {
        out.resize(frames * 2);
        for (size_t f = 0; f < frames; ++f) { out[f * 2] = int16_t(at(f, 0)); out[f * 2 + 1] = int16_t(at(f, 1)); }
        return;
    }
    const double step = double(rate) / 44100.0;
    const size_t n = frames ? size_t(double(frames - 1) / step) + 1 : 0;
    out.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const double pos = double(i) * step;
        const size_t f = size_t(pos);
        const double t = pos - double(f);
        const size_t g = std::min(f + 1, frames - 1);
        for (int c = 0; c < 2; ++c)
            out[i * 2 + size_t(c)] = int16_t(std::lround(double(at(f, c)) * (1 - t) + double(at(g, c)) * t));
    }
}

} // namespace

CdTracks::CdTracks(const std::string& dataDir) {
    if (dataDir.empty()) return;
    const fs::path base(dataDir);
    std::vector<fs::path> dirs;
    if (const char* e = std::getenv("PITFALL_MUSIC_DIR"); e && *e) dirs.emplace_back(e);   // (a folder of rips elsewhere: that one alone)
    else {
        if (fs::path a = childCI(base, "assets"); !a.empty())
            if (fs::path m = childCI(a, "music"); !m.empty()) dirs.push_back(m);
        if (fs::path m = childCI(base, "music"); !m.empty()) dirs.push_back(m);
        dirs.push_back(base);
    }
    for (const fs::path& d : dirs) {
        std::error_code ec;
        std::map<int, std::string> found;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string ext = lower(e.path().extension().string());
            if (ext != ".wav" && ext != ".ogg") continue;
            const int t = trackOf(e.path().stem().string());
            if (t < 2 || t > 99) continue;
            auto it = found.find(t);
            if (it == found.end() || ext == ".wav") found[t] = e.path().string();   // a WAV over an Ogg
        }
        if (!found.empty()) { files_ = std::move(found); dir_ = d.string(); return; }
    }
}

MusicPcm CdTracks::load(int track) {
    for (const auto& c : cache_) if (c.first == track) return c.second;
    auto it = files_.find(track);
    if (it == files_.end()) return nullptr;
    auto pcm = std::make_shared<std::vector<int16_t>>();
    const std::string ext = lower(fs::path(it->second).extension().string());
    const bool ok = ext == ".ogg" ? decodeOggFile(it->second, *pcm) : decodeWavFile(it->second, *pcm);
    if (!ok || pcm->empty()) { std::fprintf(stderr, "music: cannot decode %s\n", it->second.c_str()); return nullptr; }
    cache_.insert(cache_.begin(), {track, pcm});
    if (cache_.size() > 2) cache_.pop_back();
    return pcm;
}

bool CdTracks::decodeWavFile(const std::string& path, std::vector<int16_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    int format = 0, channels = 0, rate = 0, bits = 0;
    size_t data = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= d.size();) {
        const uint32_t len = u32(o + 4);
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && len >= 16 && o + 8 + 16 <= d.size()) {
            format = int(u16(o + 8)); channels = int(u16(o + 10)); rate = int(u32(o + 12)); bits = int(u16(o + 22));
            if (format == 0xfffe && len >= 26 && o + 8 + 26 <= d.size()) format = int(u16(o + 8 + 24));   // WAVE_FORMAT_EXTENSIBLE
        } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
            data = o + 8;
            dataLen = std::min<size_t>(len, d.size() - data);
        }
        o += 8 + len + (len & 1);
    }
    if (!data || channels < 1 || rate < 1000 || (format != 1 && format != 3)) return false;
    const int bytes = bits / 8;
    if (bytes < 1 || bytes > 4 || (format == 3 && bits != 32)) return false;
    const size_t n = dataLen / size_t(bytes);
    std::vector<int32_t> s(n - n % size_t(channels));
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t* p = d.data() + data + i * size_t(bytes);
        int32_t v;
        if (format == 3) { float x; std::memcpy(&x, p, 4); v = int32_t(std::clamp(x, -1.0f, 1.0f) * 32767.0f); }
        else if (bytes == 1) v = (int32_t(p[0]) - 128) << 8;
        else if (bytes == 2) v = int16_t(p[0] | p[1] << 8);
        else if (bytes == 3) v = int32_t(uint32_t(p[0] << 8 | p[1] << 16 | p[2] << 24)) >> 16;
        else v = int32_t(uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24)) >> 16;
        s[i] = v;
    }
    toCdFormat(s, channels, rate, out);
    return true;
}

bool CdTracks::decodeOggFile(const std::string& path, std::vector<int16_t>& out) {
    int channels = 0, rate = 0;
    short* pcm = nullptr;
    const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &pcm);
    if (frames <= 0 || !pcm || channels < 1) { std::free(pcm); return false; }
    std::vector<int32_t> s(size_t(frames) * size_t(channels));
    for (size_t i = 0; i < s.size(); ++i) s[i] = pcm[i];
    std::free(pcm);
    toCdFormat(s, channels, rate, out);
    return true;
}

} // namespace engine
