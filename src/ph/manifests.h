// Load-order manifests for the .PH asset streams.
//
// The original engine has no directory inside a .PH file; it reads records
// strictly in the order hard-coded in PITFALL.EXE.  These tables are that
// order, recovered from the decompiled loaders:
//   FUN_004457d0  - INIT.PH sprite/font load order (120 records)
//   FUN_0043ce10  - INIT.PH sound-effect load order (138 RIFF/WAV records)
//   FUN_004460e0  - per-level data record order (bg, trg, det, dt2, als, parallax bg)
//   FUN_00447190  - per-level sprite load order
#pragma once
#include <string>
#include <vector>

namespace ph {

enum class RecordKind { Sprite, Raw };

struct ManifestEntry {
    RecordKind kind;
    std::string name;
};

// Names of the six (optionally seven) level data records.  Empty = absent in file.
struct LevelFiles {
    std::string bg;        // main tile layer  ("forest1.bg")
    std::string trg;       // trigger table    ("forest1.trg")  - opaque game-logic blob
    std::string det;       // detail table     ("forest1.det")  - opaque game-logic blob
    std::string dt2;       // detail table 2   ("forest1.dt2")  - opaque game-logic blob
    std::string als;       // alien (enemy) placement list ("forest1.als") - opaque blob
    std::string parallax;  // parallax tile layer ("for_par.bg")
    std::string extra;     // second background used by level 15 (title) and 22 (room)
};

struct SoundEntry {
    std::string name;   // original file name, e.g. "SOUND01.WAV"
    int id;             // sound slot id used by game logic (0..255)
};

const std::vector<ManifestEntry>& initManifest();      // INIT.PH records 0..119
const std::vector<SoundEntry>&    soundManifest();     // INIT.PH records 120..257
const std::vector<LevelFiles>&    levelFiles();        // index = level number 0..24
const std::vector<std::string>&   levelSprites(int level);
const char*                       levelName(int level);

constexpr int kLevelCount = 25;

} // namespace ph
