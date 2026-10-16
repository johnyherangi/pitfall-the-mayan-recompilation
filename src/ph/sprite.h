// Sprite and raw-image records from the .PH streams.
//
// Sprite record body (FUN_004453a0 / blitter FUN_00438140):
//   u32 magic 0x34561234
//   u32 pal_off                  offset of rgb24[ncolors]
//   u16 ncolors
//   u16 nframes
//   u32 frame_off[nframes]       relative to body + 0x10
//   frames:  i32 w, i32 h, i32 hot_x, i32 hot_y, then RLE rows:
//              b < 0x7e      skip b transparent pixels
//              b == 0x7f     end of row
//              b == 0x7e     end of frame
//              b >= 0x80     (b - 0x7f) literal pixels follow
// Pixel values are *local* palette indices; the loader remaps them to global
// palette indices in place (we keep that behaviour, see remapInPlace()).
//
// Raw record body (FUN_004455f0) - used for the font and the score digits:
//   u32 magic ; u32 pal_off ; u16 ncolors ; u16 cell_bytes ; u16 cell_w ; u16 cell_h
//   u8 pixels[pal_off - 0x10] ; rgb24 palette[ncolors]
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ph {

constexpr uint32_t kRecordMagic = 0x34561234;

// RLE opcodes (shared by decoder and blitter).
constexpr uint8_t kRleEndFrame = 0x7e;
constexpr uint8_t kRleEndRow   = 0x7f;

struct SpriteFrame {
    int32_t w = 0, h = 0;
    int32_t hotX = 0, hotY = 0;    // draw position = entity pos + hot (see blitter for flips)
    uint32_t rleOffset = 0;        // offset of the RLE stream inside Sprite::bytes()
};

class Sprite {
public:
    Sprite() = default;
    Sprite(std::string name, std::span<const uint8_t> body);

    const std::string& name() const { return name_; }
    int frameCount() const { return int(frames_.size()); }
    const SpriteFrame& frame(int i) const { return frames_[i]; }
    int colourCount() const { return ncolors_; }
    const uint8_t* localPalette() const { return bytes_.data() + palOff_; }   // rgb24 * ncolors
    const uint8_t* rle(int frame) const { return bytes_.data() + frames_[frame].rleOffset; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }

    // Apply a local->global palette remap to every literal pixel (FUN_004453a0 loop).
    void remapInPlace(const uint8_t remap[256]);

    // Where the record sits in the original's load block (Arena::kAddress + its offset), the
    // address its sprite slot and the records that show it hold; 0 when not loaded there.
    uint32_t arenaAddr() const { return arenaAddr_; }
    void setArenaAddr(uint32_t a) { arenaAddr_ = a; }
    // The word FUN_004453a0 puts at the record's +8 for the slot its load filled (FUN_004361ee:
    // 4 + 12 * the slot's entry in the box table at 0x468824, 0xffff without one), from which
    // FUN_00424af1 takes the box; -1 when the record was not loaded through such a call.
    int boxWord() const { return boxWord_; }
    void setBoxWord(int w) { boxWord_ = w; }

    // Decode a frame into an 8-bit index bitmap; 0xff marks transparent pixels.
    std::vector<uint8_t> decode(int frame) const;

private:
    std::string name_;
    std::vector<uint8_t> bytes_;
    std::vector<SpriteFrame> frames_;
    uint32_t palOff_ = 0;
    int ncolors_ = 0;
    uint32_t arenaAddr_ = 0;
    int boxWord_ = -1;
};

class RawImage {
public:
    RawImage() = default;
    RawImage(std::string name, std::span<const uint8_t> body);

    const std::string& name() const { return name_; }
    int cellW() const { return cellW_; }
    int cellH() const { return cellH_; }
    int cellBytes() const { return cellBytes_; }
    int cellCount() const { return cellBytes_ ? int(pixels_.size() / cellBytes_) : 0; }
    const uint8_t* cell(int i) const { return pixels_.data() + size_t(i) * cellBytes_; }
    int colourCount() const { return ncolors_; }
    const uint8_t* localPalette() const { return pal_.data(); }
    const std::vector<uint8_t>& pixels() const { return pixels_; }
    void remapInPlace(const uint8_t remap[256]);

private:
    std::string name_;
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> pal_;
    int cellW_ = 0, cellH_ = 0, cellBytes_ = 0, ncolors_ = 0;
};

} // namespace ph
