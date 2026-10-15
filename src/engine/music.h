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

// What plays the CD's tracks for the game: the disc itself (a front end's MCI) or, without it,
// the ripped tracks mixed in with the samples.
class CdPlayer {
public:
    virtual ~CdPlayer() = default;
    virtual bool available() const = 0;   // DAT_0046f0cc: the disc (or its tracks) is there
    // FUN_00441b80: a track from its start to the next one's (and again at its end: the disc's
    // MM_MCINOTIFY, Game::cdTrackEnded; the files loop); 0 stops it.
    virtual void play(int track) = 0;
    virtual bool playing() const = 0;     // DAT_0046e10c
    // FUN_00441c50: auxSetVolume of 0 .. 0xffff a channel; hasVolume (DAT_0046f0a8) whether the
    // line's volume can be set at all (FUN_00442190 fades only then).
    virtual bool hasVolume() const = 0;
    virtual void setVolume(int v) = 0;
};

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
