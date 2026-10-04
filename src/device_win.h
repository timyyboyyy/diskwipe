#pragma once
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "device.h"

namespace dw {

// Physisches Laufwerk mit Rohzugriff. Alle Volumes darauf bleiben gesperrt und
// ausgehängt, solange das Objekt lebt. Benötigt Adminrechte.
class WinPhysicalDevice : public BlockDevice {
public:
    static std::unique_ptr<WinPhysicalDevice> open(int diskNumber, std::string& err);
    ~WinPhysicalDevice() override;
    WinPhysicalDevice(const WinPhysicalDevice&) = delete;
    WinPhysicalDevice& operator=(const WinPhysicalDevice&) = delete;

    uint64_t size() const override { return size_; }
    uint32_t sectorSize() const override { return sector_; }
    bool read(uint64_t offset, uint8_t* buf, size_t len) override;
    bool write(uint64_t offset, const uint8_t* buf, size_t len) override;
    bool flush() override;
    std::string lastError() const override { return err_; }

private:
    WinPhysicalDevice() = default;

    HANDLE disk_ = INVALID_HANDLE_VALUE;
    std::vector<HANDLE> volumes_;
    uint64_t size_ = 0;
    uint32_t sector_ = 512;
    std::string err_;
};

}  // namespace dw
