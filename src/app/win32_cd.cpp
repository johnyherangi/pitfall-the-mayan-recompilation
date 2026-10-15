// The game's CD through MCI, as the original plays it:
//
//   FUN_00441cf0  "cdaudio" opened, the disc known by the lengths of tracks 7 (4:07:33) and 8
//                 (4:32:12) in MSF, then the time format TMSF; the CD line's volume found among
//                 the aux devices (AUXCAPS_CDAUDIO, else AUXCAPS_AUXIN) and set, the old one kept
//   FUN_00441b80  MCI_PLAY from a track to the next with MCI_NOTIFY (MM_MCINOTIFY at its end, the
//                 window plays it again: Game::cdTrackEnded); track 0 MCI_STOP
//   FUN_00441c50  auxSetVolume, Option_CDVolume (0 .. 100) times 0x28f a channel
//   FUN_004420e0  at the end: stopped, closed, the line's volume put back
//
// Without the disc the ripped tracks play instead (the game's own player).
#include "app/win32_app.h"
#include "engine/music.h"

#include <mmsystem.h>

#include <memory>

namespace app {
namespace {

class DiscPlayer : public engine::CdPlayer {
public:
    explicit DiscPlayer(HWND notify) : notify_(notify) {}
    ~DiscPlayer() override {
        // FUN_004420e0
        if (device_) {
            if (playing_) mciSendCommandA(device_, MCI_STOP, 0, 0);
            mciSendCommandA(device_, MCI_CLOSE, 0, 0);
        }
        if (auxVolume_) auxSetVolume(aux_, savedVolume_);
    }

    bool open() {
        // FUN_00441cf0
        MCI_OPEN_PARMSA op{};
        op.lpstrDeviceType = "cdaudio";
        if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE, reinterpret_cast<DWORD_PTR>(&op)) != 0) return false;
        device_ = op.wDeviceID;
        MCI_SET_PARMS set{};
        set.dwTimeFormat = MCI_FORMAT_MSF;
        if (mciSendCommandA(device_, MCI_SET, MCI_SET_TIME_FORMAT, reinterpret_cast<DWORD_PTR>(&set)) != 0) return close();
        auto length = [&](int track, int m, int s, int f) {
            MCI_STATUS_PARMS st{};
            st.dwItem = MCI_STATUS_LENGTH;
            st.dwTrack = DWORD(track);
            if (mciSendCommandA(device_, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, reinterpret_cast<DWORD_PTR>(&st)) != 0) return false;
            return MCI_MSF_MINUTE(st.dwReturn) == m && MCI_MSF_SECOND(st.dwReturn) == s && MCI_MSF_FRAME(st.dwReturn) == f;
        };
        if (!length(8, 4, 0x20, 0x0c) || !length(7, 4, 0x07, 0x21)) return close();
        set.dwTimeFormat = MCI_FORMAT_TMSF;
        if (mciSendCommandA(device_, MCI_SET, MCI_SET_TIME_FORMAT, reinterpret_cast<DWORD_PTR>(&set)) != 0) return close();
        // the CD line's volume: a CD audio aux device, else a line in
        const UINT n = auxGetNumDevs();
        bool found = false;
        for (WORD tech : {WORD(AUXCAPS_CDAUDIO), WORD(AUXCAPS_AUXIN)}) {
            for (UINT i = 0; i < n && !found; ++i) {
                AUXCAPSA caps{};
                if (auxGetDevCapsA(i, &caps, sizeof caps) != MMSYSERR_NOERROR) break;
                if (caps.wTechnology == tech) { found = true; aux_ = i; }
            }
            if (found) break;
        }
        if (found) {
            AUXCAPSA caps{};
            if (auxGetDevCapsA(aux_, &caps, sizeof caps) == MMSYSERR_NOERROR && (caps.dwSupport & AUXCAPS_VOLUME) &&
                auxGetVolume(aux_, &savedVolume_) == MMSYSERR_NOERROR)
                auxVolume_ = true;
        }
        return true;
    }

    bool available() const override { return true; }
    void play(int track) override {
        // FUN_00441b80
        if (track != 0) {
            MCI_PLAY_PARMS p{};
            p.dwCallback = reinterpret_cast<DWORD_PTR>(notify_);
            p.dwFrom = MCI_MAKE_TMSF(track & 0xff, 0, 0, 0);
            p.dwTo = MCI_MAKE_TMSF((track + 1) & 0xff, 0, 0, 0);
            mciSendCommandA(device_, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO, reinterpret_cast<DWORD_PTR>(&p));
            playing_ = true;
            return;
        }
        if (playing_) { mciSendCommandA(device_, MCI_STOP, 0, 0); playing_ = false; }
    }
    bool playing() const override { return playing_; }
    bool hasVolume() const override { return auxVolume_; }
    void setVolume(int v) override {
        // FUN_00441c50: the same on both channels
        if (auxVolume_) auxSetVolume(aux_, DWORD(v & 0xffff) * 0x10001u);
    }
    MCIDEVICEID device() const { return device_; }

private:
    bool close() {
        mciSendCommandA(device_, MCI_CLOSE, 0, 0);
        device_ = 0;
        return false;
    }
    HWND notify_;
    MCIDEVICEID device_ = 0;
    bool playing_ = false;               // DAT_0046e10c
    UINT aux_ = 0;                       // DAT_0046f0a0
    bool auxVolume_ = false;             // DAT_0046f0a8
    DWORD savedVolume_ = 0;              // DAT_0046f0ac
};

MCIDEVICEID g_disc = 0;

} // namespace

std::unique_ptr<engine::CdPlayer> openDisc(HWND notify) {
    auto d = std::make_unique<DiscPlayer>(notify);
    if (!d->open()) return nullptr;
    g_disc = d->device();
    return d;
}

unsigned discDevice() { return g_disc; }

} // namespace app
