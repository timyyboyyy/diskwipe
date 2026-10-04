#pragma once
#include <cstddef>
#include <cstdint>

namespace dw {

enum class PatternKind { Zero, Random };

// Größe eines Schreib-/Leseblocks. Das Zufallsmuster wird pro Block neu initialisiert.
constexpr size_t kBlockSize = size_t(1) << 20;

// Füllt buf (len <= kBlockSize) mit dem Muster für Block blockIndex.
// Ein kürzerer Puffer erhält exakt den Anfang des vollen Blocks.
void fillPattern(uint8_t* buf, size_t len, PatternKind kind, uint64_t seed, uint64_t blockIndex);

// 64-Bit-Seed aus der kryptografischen Zufallsquelle des Systems. Wirft std::runtime_error.
uint64_t secureRandomSeed();

}  // namespace dw
