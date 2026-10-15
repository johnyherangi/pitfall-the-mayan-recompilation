#include "ph/sprite.h"

#include "ph/ph_stream.h"

namespace ph {

Sprite::Sprite(std::string name, std::span<const uint8_t> body)
    : name_(std::move(name)), bytes_(body.begin(), body.end()) {
    if (bytes_.size() < 12 || rd32(bytes_.data()) != kRecordMagic)
        throw Error("Missing Sprite: " + name_);           // string at 0x46cff0
    palOff_ = rd32(&bytes_[4]);
    ncolors_ = rd16(&bytes_[8]);
    const int nframes = rd16(&bytes_[10]);
    if (bytes_.size() < 12 + size_t(nframes) * 4 || palOff_ + size_t(ncolors_) * 3 > bytes_.size())
        throw Error("Bad sprite record: " + name_);
    frames_.resize(nframes);
    for (int i = 0; i < nframes; ++i) {
        const uint32_t off = rd32(&bytes_[12 + i * 4]);
        if (off + 16 > bytes_.size()) throw Error("Bad frame offset in sprite " + name_);
        SpriteFrame& f = frames_[i];
        f.w = rds32(&bytes_[off]);
        f.h = rds32(&bytes_[off + 4]);
        f.hotX = rds32(&bytes_[off + 8]);
        f.hotY = rds32(&bytes_[off + 12]);
        f.rleOffset = off + 16;
    }
}

void Sprite::remapInPlace(const uint8_t remap[256]) {
    // FUN_004453a0: walk every frame's RLE stream and remap literal runs.
    for (const SpriteFrame& f : frames_) {
        uint8_t* p = bytes_.data() + f.rleOffset;
        uint8_t* end = bytes_.data() + bytes_.size();
        while (p < end) {
            const uint8_t b = *p++;
            if (b == kRleEndFrame) break;
            if (b < 0x80) continue;               // skip-run or end-of-row
            int n = b - 0x7f;
            while (n-- && p < end) { *p = remap[*p]; ++p; }
        }
    }
}

std::vector<uint8_t> Sprite::decode(int frame) const {
    const SpriteFrame& f = frames_[frame];
    std::vector<uint8_t> out(size_t(f.w) * f.h, 0xff);
    const uint8_t* p = rle(frame);
    const uint8_t* end = bytes_.data() + bytes_.size();
    int x = 0, y = 0;
    while (p < end) {
        const uint8_t b = *p++;
        if (b == kRleEndFrame) break;
        if (b == kRleEndRow) { x = 0; ++y; continue; }
        if (b < 0x80) { x += b; continue; }
        int n = b - 0x7f;
        while (n-- && p < end) {
            if (x >= 0 && x < f.w && y >= 0 && y < f.h) out[size_t(y) * f.w + x] = *p;
            ++p; ++x;
        }
    }
    return out;
}

RawImage::RawImage(std::string name, std::span<const uint8_t> body) : name_(std::move(name)) {
    if (body.size() < 16 || rd32(body.data()) != kRecordMagic) throw Error("Bad raw image: " + name_);
    const uint32_t palOff = rd32(&body[4]);
    ncolors_ = rd16(&body[8]);
    cellBytes_ = rd16(&body[10]);
    cellW_ = rd16(&body[12]);
    cellH_ = rd16(&body[14]);
    if (palOff < 16 || palOff + size_t(ncolors_) * 3 > body.size()) throw Error("Bad raw image: " + name_);
    pixels_.assign(body.begin() + 16, body.begin() + palOff);
    pal_.assign(body.begin() + palOff, body.begin() + palOff + size_t(ncolors_) * 3);
}

void RawImage::remapInPlace(const uint8_t remap[256]) {
    for (auto& px : pixels_) px = remap[px];     // FUN_004455f0 loop
}

} // namespace ph
