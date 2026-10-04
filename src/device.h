#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace dw {

// Roher, blockweise adressierbarer Datenträger.
// read/write geben false zurück und setzen lastError(); sie werfen nicht.
class BlockDevice {
public:
    virtual ~BlockDevice() = default;
    virtual uint64_t size() const = 0;
    virtual uint32_t sectorSize() const = 0;
    virtual bool read(uint64_t offset, uint8_t* buf, size_t len) = 0;
    virtual bool write(uint64_t offset, const uint8_t* buf, size_t len) = 0;
    virtual bool flush() = 0;
    virtual std::string lastError() const = 0;
};

}  // namespace dw
