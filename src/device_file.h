#pragma once
#include <fstream>
#include <string>

#include "device.h"

namespace dw {

// Image-Datei als Datenträger (Tests und Testwerkzeug). Die Datei muss existieren.
class FileDevice : public BlockDevice {
public:
    explicit FileDevice(const std::string& path, uint32_t sectorSize = 512);

    uint64_t size() const override { return size_; }
    uint32_t sectorSize() const override { return sector_; }
    bool read(uint64_t offset, uint8_t* buf, size_t len) override;
    bool write(uint64_t offset, const uint8_t* buf, size_t len) override;
    bool flush() override;
    std::string lastError() const override { return err_; }

private:
    bool inRange(uint64_t offset, size_t len);

    std::fstream file_;
    uint64_t size_ = 0;
    uint32_t sector_;
    std::string err_;
};

}  // namespace dw
