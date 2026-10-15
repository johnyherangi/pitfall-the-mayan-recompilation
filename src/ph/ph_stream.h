// Sequential reader for .PH asset streams.
//
// File layout (reverse engineered from FUN_00444fe0 / FUN_004453a0 / FUN_004455f0):
//     repeat { uint32 size; uint8 body[size]; }
// Records carry no names; callers must know the order (see manifests.h).
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ph {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class PhStream {
public:
    PhStream() = default;
    explicit PhStream(std::vector<uint8_t> bytes) : data_(std::move(bytes)) {}

    static PhStream fromFile(const std::string& path);

    // Returns the next record body (view into the stream's storage).
    std::span<const uint8_t> next();
    bool atEnd() const { return pos_ + 4 > data_.size(); }
    size_t position() const { return pos_; }
    size_t size() const { return data_.size(); }
    int recordIndex() const { return index_; }
    void rewind() { pos_ = 0; index_ = 0; }

private:
    std::vector<uint8_t> data_;
    size_t pos_ = 0;
    int index_ = 0;
};

// Little-endian helpers used throughout the loaders.
inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline int32_t rds32(const uint8_t* p) { return int32_t(rd32(p)); }

} // namespace ph
