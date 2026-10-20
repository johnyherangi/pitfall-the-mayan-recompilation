// phimport <input> ... <out-dir>: the original game's files, music and intro taken out of whatever
// the disc was kept as, into the folder the game runs from (original/).
//
// An input is a folder (the CD's, or one with INIT.PH; every file is copied), a .zip (opened, its
// contents taken as inputs), an .iso (the disc's data track, ISO 9660, 2048-byte sectors), or a
// .cue with its .img / .bin (a CloneCD / CDRWIN image: the data track in 2352-byte sectors, and
// the audio tracks, which are the game's music: each goes to ASSETS/MUSIC/TrackNN as an Ogg
// Vorbis file when ffmpeg is on the path, else as a WAV).  With ffmpeg the Indeo 3.2 INTRO.AVI is
// also converted to ASSETS/intro.converted.avi (Cinepak), which the launcher plays where Indeo
// cannot be decoded.  Nothing already in the output folder is written again (--force does: the
// tracks cut again replace what was there, an Ogg the WAV of an import without ffmpeg).
//
//   phimport assets-in original          (a folder of inputs: each is taken in turn)
//   phimport Pitfall.cue original
//
// The build runs it (import_assets) when original/INIT.PH is missing and assets-in/ has something.
#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool g_force = false;
std::string g_ffmpeg;                                     // "" when there is none
int g_files = 0, g_tracks = 0;

std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string upper(std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }

bool there(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }

// INIT.PH in a folder, any case
bool hasInit(const fs::path& dir) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && upper(e.path().filename().string()) == "INIT.PH") return true;
    return false;
}

bool writeFile(const fs::path& p, const void* data, size_t n) {
    if (there(p) && !g_force) return true;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "phimport: cannot write %s\n", p.string().c_str()); return false; }
    f.write(static_cast<const char*>(data), std::streamsize(n));
    ++g_files;
    return true;
}

// ---------------------------------------------------------------- ffmpeg --
std::string findFfmpeg() {
    if (const char* e = std::getenv("PITFALL_FFMPEG"); e && *e) return e;
#ifdef _WIN32
    const char* probe = "where ffmpeg >NUL 2>NUL";
#else
    const char* probe = "command -v ffmpeg >/dev/null 2>&1";
#endif
    return std::system(probe) == 0 ? "ffmpeg" : "";
}

std::string shq(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string q = "'";
    for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; }
    return q + "'";
#endif
}

// --------------------------------------------------------------- ISO 9660 --
// Sectors read from a file: raw 2048-byte ones (.iso) or 2352-byte ones with the user data at an
// offset (MODE1: 16, MODE2 form 1: 24).
struct Sectors {
    std::ifstream f;
    long long start = 0;                                  // the data track's first byte in the file
    int size = 2048, dataAt = 0;
    bool read(uint32_t lba, uint8_t* out) {
        f.seekg(start + (long long)lba * size + dataAt);
        return bool(f.read(reinterpret_cast<char*>(out), 2048));
    }
};

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

void isoDirectory(Sectors& s, uint32_t lba, uint32_t len, const fs::path& out, int depth) {
    if (depth > 8) return;
    std::vector<uint8_t> dir(((len + 2047) / 2048) * 2048);
    for (uint32_t i = 0; i < dir.size() / 2048; ++i) if (!s.read(lba + i, dir.data() + i * 2048)) return;
    size_t o = 0;
    while (o + 33 <= len) {
        const uint8_t recLen = dir[o];
        if (recLen == 0) { o = (o / 2048 + 1) * 2048; continue; }   // (the rest of the sector is empty)
        if (o + recLen > dir.size()) break;
        const uint8_t* r = dir.data() + o;
        const uint32_t extent = le32(r + 2), size = le32(r + 10);
        const uint8_t flags = r[25], nameLen = r[32];
        std::string name(reinterpret_cast<const char*>(r + 33), nameLen);
        o += recLen;
        if (nameLen == 1 && (name[0] == 0 || name[0] == 1)) continue;   // . and ..
        if (const size_t semi = name.find(';'); semi != std::string::npos) name.erase(semi);
        if (!name.empty() && name.back() == '.') name.pop_back();
        if (flags & 2) { isoDirectory(s, extent, size, out / name, depth + 1); continue; }
        const fs::path p = out / name;
        if (there(p) && !g_force) continue;
        std::vector<uint8_t> data(((size + 2047) / 2048) * 2048);
        for (uint32_t i = 0; i < data.size() / 2048; ++i) if (!s.read(extent + i, data.data() + i * 2048)) { data.clear(); break; }
        if (data.empty()) { std::fprintf(stderr, "phimport: %s: cannot read\n", name.c_str()); continue; }
        writeFile(p, data.data(), size);
    }
}

bool isoExtract(Sectors& s, const fs::path& out) {
    uint8_t pvd[2048];
    if (!s.read(16, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0 || pvd[0] != 1) return false;
    const uint8_t* root = pvd + 156;                      // the root directory record
    isoDirectory(s, le32(root + 2), le32(root + 10), out, 0);
    return true;
}

// --------------------------------------------------------------- the disc --
struct Track { int n = 0; std::string type; long long index0 = -1, index1 = -1; };   // frames of 1/75 s

std::vector<Track> parseCue(const fs::path& cue, std::string& imgName) {
    std::vector<Track> tracks;
    std::ifstream f(cue);
    std::string line;
    while (std::getline(f, line)) {
        int n = 0, idx = 0, mm = 0, ss = 0, ff = 0;
        char type[32] = {};
        if (line.find("FILE") != std::string::npos && line.find('"') != std::string::npos) {
            const size_t q1 = line.find('"'), q2 = line.rfind('"');
            if (q2 > q1) imgName = line.substr(q1 + 1, q2 - q1 - 1);
        } else if (std::sscanf(line.c_str(), " TRACK %d %31s", &n, type) == 2) {
            tracks.push_back(Track{n, type});
        } else if (!tracks.empty() && std::sscanf(line.c_str(), " INDEX %d %d:%d:%d", &idx, &mm, &ss, &ff) == 4) {
            const long long frames = (mm * 60LL + ss) * 75 + ff;
            if (idx == 0) tracks.back().index0 = frames; else if (idx == 1) tracks.back().index1 = frames;
        }
    }
    return tracks;
}

void writeWavHeader(std::ofstream& f, uint32_t dataBytes) {
    uint8_t h[44] = {'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0, 0x44,0xac,0,0, 0x10,0xb1,2,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0};
    const uint32_t riff = 36 + dataBytes;
    std::memcpy(h + 4, &riff, 4); std::memcpy(h + 40, &dataBytes, 4);
    f.write(reinterpret_cast<const char*>(h), 44);
}

// An audio track's frames, through ffmpeg to Ogg (ASSETS/MUSIC/TrackNN.ogg) or as a WAV.  The
// track's other file goes (with --force): the game would take a WAV left there over the new Ogg.
void audioTrack(std::ifstream& img, int n, long long from, long long to, const fs::path& musicDir) {
    char name[32];
    std::snprintf(name, sizeof name, "Track%02d", n);
    const fs::path ogg = musicDir / (std::string(name) + ".ogg"), wav = musicDir / (std::string(name) + ".wav");
    if (!g_force && (there(ogg) || there(wav))) return;
    std::error_code ec;
    fs::create_directories(musicDir, ec);
    const long long bytes = (to - from) * 2352;
    img.seekg(from * 2352);
    std::vector<char> buf(static_cast<size_t>(bytes));
    if (!img.read(buf.data(), bytes)) { std::fprintf(stderr, "phimport: track %d: cannot read\n", n); return; }
    if (!g_ffmpeg.empty()) {
        const fs::path raw = musicDir / (std::string(name) + ".raw");
        { std::ofstream r(raw, std::ios::binary); r.write(buf.data(), bytes); }
        const std::string cmd = shq(g_ffmpeg) + " -y -loglevel error -f s16le -ar 44100 -ac 2 -i " + shq(raw.string()) +
                                " -c:a libvorbis -q:a 6 " + shq(ogg.string());
        const int rc = std::system(cmd.c_str());
        fs::remove(raw, ec);
        if (rc == 0 && there(ogg)) { fs::remove(wav, ec); ++g_tracks; return; }
        std::fprintf(stderr, "phimport: ffmpeg could not encode track %d; a WAV instead\n", n);
    }
    std::ofstream w(wav, std::ios::binary);
    writeWavHeader(w, uint32_t(bytes));
    w.write(buf.data(), bytes);
    w.close();
    if (w) fs::remove(ogg, ec);
    ++g_tracks;
}

bool importCue(const fs::path& cue, const fs::path& out) {
    std::string imgName;
    std::vector<Track> tracks = parseCue(cue, imgName);
    if (tracks.empty()) { std::fprintf(stderr, "phimport: %s: no tracks\n", cue.string().c_str()); return false; }
    fs::path img = cue.parent_path() / imgName;
    if (!there(img)) {                                   // (the name in the sheet differs: the same stem)
        for (const char* ext : {".img", ".bin", ".IMG", ".BIN"}) if (there(cue.parent_path() / (cue.stem().string() + ext))) img = cue.parent_path() / (cue.stem().string() + ext);
    }
    if (!there(img)) { std::fprintf(stderr, "phimport: %s: its image %s is missing\n", cue.string().c_str(), imgName.c_str()); return false; }
    std::printf("phimport: %s (%s)\n", cue.filename().string().c_str(), img.filename().string().c_str());
    const long long total = (long long)fs::file_size(img) / 2352;
    // the data track
    for (const Track& t : tracks) {
        if (t.type.rfind("MODE", 0) != 0) continue;
        Sectors s;
        s.f.open(img, std::ios::binary);
        s.start = std::max(t.index1, 0LL) * 2352;
        s.size = t.type.find("2352") != std::string::npos ? 2352 : 2048;
        s.dataAt = s.size == 2352 ? (t.type.rfind("MODE2", 0) == 0 ? 24 : 16) : 0;
        if (s.size == 2048) s.start = std::max(t.index1, 0LL) * 2048;
        if (!isoExtract(s, out)) std::fprintf(stderr, "phimport: track %d: not an ISO 9660 data track\n", t.n);
        break;
    }
    // the audio tracks: from INDEX 01 to the next track's INDEX 00 (its pregap) or 01
    std::ifstream f(img, std::ios::binary);
    for (size_t i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        if (t.type != "AUDIO" || t.index1 < 0) continue;
        long long end = total;
        if (i + 1 < tracks.size()) end = tracks[i + 1].index0 >= 0 ? tracks[i + 1].index0 : tracks[i + 1].index1;
        if (end > t.index1) audioTrack(f, t.n, t.index1, end, out / "ASSETS" / "MUSIC");
    }
    return true;
}

bool importIso(const fs::path& iso, const fs::path& out) {
    std::printf("phimport: %s\n", iso.filename().string().c_str());
    Sectors s;
    s.f.open(iso, std::ios::binary);
    if (isoExtract(s, out)) return true;
    // a raw image without a sheet: 2352-byte MODE1 sectors
    s.size = 2352; s.dataAt = 16;
    if (isoExtract(s, out)) return true;
    std::fprintf(stderr, "phimport: %s: not an ISO 9660 image\n", iso.string().c_str());
    return false;
}

bool importDir(const fs::path& dir, const fs::path& out);

bool importZip(const fs::path& zip, const fs::path& out) {
    std::printf("phimport: %s\n", zip.filename().string().c_str());
    mz_zip_archive z{};
    if (!mz_zip_reader_init_file(&z, zip.string().c_str(), 0)) { std::fprintf(stderr, "phimport: %s: cannot open\n", zip.string().c_str()); return false; }
    const fs::path tmp = out / ".import" / zip.stem();
    std::error_code ec;
    fs::create_directories(tmp, ec);
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || mz_zip_reader_is_file_a_directory(&z, i)) continue;
        const fs::path p = tmp / st.m_filename;
        fs::create_directories(p.parent_path(), ec);
        if (there(p) && fs::file_size(p, ec) == st.m_uncomp_size) continue;
        if (!mz_zip_reader_extract_to_file(&z, i, p.string().c_str(), 0)) std::fprintf(stderr, "phimport: %s: cannot extract %s\n", zip.filename().string().c_str(), st.m_filename);
    }
    mz_zip_reader_end(&z);
    const bool ok = importDir(tmp, out);
    fs::remove_all(tmp, ec);                               // (the unpacked copy: 600 MB for a disc image)
    return ok;
}

// A folder of the game's files is copied; any other folder is searched for inputs.
bool importDir(const fs::path& dir, const fs::path& out) {
    std::error_code ec;
    if (hasInit(dir)) {
        std::printf("phimport: %s (the game's files)\n", dir.string().c_str());
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const fs::path rel = fs::relative(e.path(), dir, ec), p = out / rel;
            if (there(p) && !g_force) continue;
            fs::create_directories(p.parent_path(), ec);
            if (fs::copy_file(e.path(), p, fs::copy_options::overwrite_existing, ec)) ++g_files;
        }
        return true;
    }
    bool any = false;
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(dir, ec)) entries.push_back(e.path());
    std::sort(entries.begin(), entries.end());
    // sheets first (an image's audio), then images, then archives and folders
    auto rank = [](const fs::path& p) {
        const std::string e = lower(p.extension().string());
        return e == ".cue" ? 0 : e == ".iso" ? 1 : e == ".zip" ? 2 : fs::is_directory(p) ? 3 : 9;
    };
    std::stable_sort(entries.begin(), entries.end(), [&](const fs::path& a, const fs::path& b) { return rank(a) < rank(b); });
    for (const fs::path& p : entries) {
        const std::string e = lower(p.extension().string());
        if (e == ".cue") any |= importCue(p, out);
        else if (e == ".iso") any |= importIso(p, out);
        else if (e == ".zip") any |= importZip(p, out);
        else if (fs::is_directory(p, ec) && p.filename() != ".import") any |= importDir(p, out);
        else if (e == ".img" || e == ".bin") {
            bool sheet = false;                           // (taken with its .cue; alone, as a raw image)
            for (const fs::path& q : entries) if (lower(q.extension().string()) == ".cue" && q.stem() == p.stem()) sheet = true;
            if (!sheet) any |= importIso(p, out);
        }
    }
    return any;
}

bool importOne(const fs::path& in, const fs::path& out) {
    std::error_code ec;
    const std::string e = lower(in.extension().string());
    if (fs::is_directory(in, ec)) return importDir(in, out);
    if (e == ".cue") return importCue(in, out);
    if (e == ".iso" || e == ".img" || e == ".bin") return importIso(in, out);
    if (e == ".zip") return importZip(in, out);
    std::fprintf(stderr, "phimport: %s: not a folder, .zip, .iso or .cue\n", in.string().c_str());
    return false;
}

// INTRO.AVI (Indeo 3.2) as Cinepak beside it, for the launcher on a system without Indeo
void convertIntro(const fs::path& out) {
    if (g_ffmpeg.empty()) return;
    for (const fs::path& dir : {out / "ASSETS", out}) {
        fs::path avi;
        std::error_code ec;
        for (const auto& en : fs::directory_iterator(dir, ec))
            if (en.is_regular_file(ec) && upper(en.path().filename().string()) == "INTRO.AVI") avi = en.path();
        if (avi.empty()) continue;
        const fs::path conv = dir / "intro.converted.avi";
        if (there(conv) && !g_force) continue;
        const std::string cmd = shq(g_ffmpeg) + " -y -loglevel error -i " + shq(avi.string()) +
                                " -c:v cinepak -g 1 -q:v 1 -max_strips 1 -c:a pcm_u8 -ar 11025 " + shq(conv.string());
        if (std::system(cmd.c_str()) == 0) std::printf("phimport: %s converted (Cinepak)\n", fs::relative(conv, out, ec).string().c_str());
        else std::fprintf(stderr, "phimport: ffmpeg could not convert %s\n", avi.string().c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--force") g_force = true;
        else if (a == "--no-ffmpeg") g_ffmpeg = "-";
        else inputs.emplace_back(a);
    }
    if (inputs.size() < 2) { std::fprintf(stderr, "phimport [--force] [--no-ffmpeg] <input> ... <out-dir>\n"); return 2; }
    const fs::path out = inputs.back();
    inputs.pop_back();
    if (g_ffmpeg == "-") g_ffmpeg.clear(); else g_ffmpeg = findFfmpeg();
    if (g_ffmpeg.empty())
        std::printf("phimport: no ffmpeg on the path: the music as WAV files, the intro left as it is "
                    "(with ffmpeg installed, phimport --force cuts the tracks again as Ogg)\n");
    std::error_code ec;
    fs::create_directories(out, ec);
    bool any = false;
    for (const fs::path& in : inputs) any |= importOne(in, out);
    fs::remove_all(out / ".import", ec);
    convertIntro(out);
    const bool ok = hasInit(out) || (there(out / "ASSETS") && hasInit(out / "ASSETS"));
    std::printf("phimport: %d files and %d music tracks written to %s%s\n", g_files, g_tracks, out.string().c_str(),
                ok ? "" : "; INIT.PH is still missing: no game files found in the inputs");
    return ok ? 0 : any ? 1 : 2;
}
