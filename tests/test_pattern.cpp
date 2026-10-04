#include <algorithm>
#include <array>

#include "pattern.h"
#include "test.h"

using namespace dw;

TEST(pattern_zero_is_all_zero) {
    std::vector<uint8_t> b(4096, 0xFF);
    fillPattern(b.data(), b.size(), PatternKind::Zero, 123, 7);
    CHECK(std::all_of(b.begin(), b.end(), [](uint8_t x) { return x == 0; }));
}

TEST(pattern_random_is_deterministic) {
    std::vector<uint8_t> a(1000), b(1000);
    fillPattern(a.data(), a.size(), PatternKind::Random, 42, 3);
    fillPattern(b.data(), b.size(), PatternKind::Random, 42, 3);
    CHECK(a == b);
}

TEST(pattern_random_differs_by_seed) {
    std::vector<uint8_t> a(1000), b(1000);
    fillPattern(a.data(), a.size(), PatternKind::Random, 1, 0);
    fillPattern(b.data(), b.size(), PatternKind::Random, 2, 0);
    CHECK(a != b);
}

TEST(pattern_random_differs_by_block) {
    std::vector<uint8_t> a(1000), b(1000);
    fillPattern(a.data(), a.size(), PatternKind::Random, 1, 0);
    fillPattern(b.data(), b.size(), PatternKind::Random, 1, 1);
    CHECK(a != b);
}

TEST(pattern_partial_block_is_prefix_of_full_block) {
    std::vector<uint8_t> full(kBlockSize), part(1003);
    fillPattern(full.data(), full.size(), PatternKind::Random, 99, 5);
    fillPattern(part.data(), part.size(), PatternKind::Random, 99, 5);
    CHECK(std::equal(part.begin(), part.end(), full.begin()));
}

TEST(pattern_random_histogram_roughly_uniform) {
    std::vector<uint8_t> b(kBlockSize);
    fillPattern(b.data(), b.size(), PatternKind::Random, 0xC0FFEE, 0);
    std::array<uint32_t, 256> counts{};
    for (uint8_t x : b) ++counts[x];
    for (uint32_t c : counts) CHECK(c > 3500 && c < 4700);  // Erwartung 4096
}

TEST(secure_seed_varies) {
    CHECK(secureRandomSeed() != secureRandomSeed());
}
