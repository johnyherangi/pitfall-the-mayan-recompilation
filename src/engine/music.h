// CD audio.
//
// The original plays the disc's audio tracks with MCI ("cdaudio"; FUN_00441cf0 opens it and
// checks the disc by the lengths of tracks 7 and 8): FUN_00441b80 plays one track (from it to
// the next, TMSF) and the window plays it again when MCI reports its end (MM_MCINOTIFY);
// FUN_00441c50 sets the CD line's volume with auxSetVolume (Option_CDVolume 0..100 times 0x28f
// a channel) and FUN_00442190 fades it out over two seconds at a level load.
//
// Here the tracks are files ripped from the disc: TrackNN.wav or TrackNN.ogg (also "Track NN",
// "NN - title" and the like), NN = 02 .. 23, in $PITFALL_MUSIC_DIR, <data>/ASSETS/MUSIC,
// <data>/MUSIC or <data>.
// Without them the game is silent where the CD would play, as the original without its disc.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine {

using MusicPcm = std::shared_ptr<const std::vector<int16_t>>;   // 44.1 kHz stereo, interleaved

class CdTracks {
public:
    explicit CdTracks(const std::string& dataDir = {});
    bool available() const { return !files_.empty(); }          // DAT_0046f0cc: the disc is there
    bool has(int track) const { return files_.count(track) != 0; }
    const std::string& directory() const { return dir_; }
    // The track decoded (kept while it is one of the last two asked for); null without it.
    MusicPcm load(int track);

    // Decoders (exposed for the tools and tests): any PCM WAV / any Ogg Vorbis file to 44.1 kHz
    // stereo 16-bit.
    static bool decodeWavFile(const std::string& path, std::vector<int16_t>& out);
    static bool decodeOggFile(const std::string& path, std::vector<int16_t>& out);

private:
    std::string dir_;
    std::map<int, std::string> files_;
    std::vector<std::pair<int, MusicPcm>> cache_;
};

} // namespace engine
