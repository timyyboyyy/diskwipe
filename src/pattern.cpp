#include "pattern.h"

#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fstream>
#endif

namespace dw {
namespace {

uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

// xoshiro256** (Blackman/Vigna)
class Xoshiro256ss {
public:
    Xoshiro256ss(uint64_t seed, uint64_t blockIndex) {
        uint64_t x = seed ^ (blockIndex * 0xD1B54A32D192ED03ULL);
        for (uint64_t& v : s_) v = splitmix64(x);
    }
    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

private:
    uint64_t s_[4];
};

}  // namespace

void fillPattern(uint8_t* buf, size_t len, PatternKind kind, uint64_t seed, uint64_t blockIndex) {
    if (kind == PatternKind::Zero) {
        std::memset(buf, 0, len);
        return;
    }
    Xoshiro256ss rng(seed, blockIndex);
    for (size_t i = 0; i < len; i += 8) {
        const uint64_t v = rng.next();
        for (size_t b = 0; b < 8 && i + b < len; ++b) buf[i + b] = static_cast<uint8_t>(v >> (8 * b));
    }
}

uint64_t secureRandomSeed() {
    uint64_t v = 0;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("BCryptGenRandom fehlgeschlagen");
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(&v), sizeof(v))) throw std::runtime_error("/dev/urandom nicht lesbar");
#endif
    return v;
}

}  // namespace dw
