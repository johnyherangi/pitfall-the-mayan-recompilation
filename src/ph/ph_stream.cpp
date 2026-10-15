#include "ph/ph_stream.h"

#include <cctype>
#include <fstream>
#include <iterator>

namespace ph {

PhStream PhStream::fromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // The original names are lower case in the binary but the CD has them
        // in upper case; Windows does not care, other file systems do.
        std::string alt = path;
        const size_t slash = alt.find_last_of("/\\");
        for (size_t i = (slash == std::string::npos ? 0 : slash + 1); i < alt.size(); ++i)
            alt[i] = char(std::toupper(static_cast<unsigned char>(alt[i])));
        f.open(alt, std::ios::binary);
    }
    if (!f) throw Error("Could not find file " + path);   // string resource #9
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return PhStream(std::move(bytes));
}

std::span<const uint8_t> PhStream::next() {
    if (pos_ + 4 > data_.size()) throw Error("PH stream: unexpected end of file");
    const uint32_t size = rd32(&data_[pos_]);
    pos_ += 4;
    if (size > data_.size() - pos_) throw Error("PH stream: record overruns file");
    std::span<const uint8_t> body(&data_[pos_], size);
    pos_ += size;
    ++index_;
    return body;
}

} // namespace ph
