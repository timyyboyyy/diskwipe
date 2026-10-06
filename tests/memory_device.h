#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "device.h"

// Speicher-Datenträger für Tests. realSize < reportedSize simuliert einen Fake-Stick:
// Adressen werden modulo realSize abgebildet.
class MemoryDevice : public dw::BlockDevice {
public:
    static constexpr uint64_t kNoFault = UINT64_MAX;

    MemoryDevice(uint64_t reportedSize, uint32_t sectorSize = 512, uint64_t realSize = 0)
        : reported_(reportedSize), sector_(sectorSize), data_(realSize ? realSize : reportedSize, 0x5A) {}

    uint64_t failWriteAt = kNoFault;
    uint64_t failReadAt = kNoFault;
    int failFlushNumber = -1;           // 1-basiert: dieser flush()-Aufruf schlägt fehl
    bool loseUnflushedOnFault = false;  // Abstecken: Schreibcache des Sticks geht verloren
    int flushCount = 0;

    uint64_t size() const override { return reported_; }
    uint32_t sectorSize() const override { return sector_; }

    bool read(uint64_t off, uint8_t* buf, size_t n) override {
        if (hits(failReadAt, off, n)) return fail("simulierter Lesefehler");
        for (size_t i = 0; i < n; ++i) buf[i] = data_[(off + i) % data_.size()];
        return true;
    }
    bool write(uint64_t off, const uint8_t* buf, size_t n) override {
        if (hits(failWriteAt, off, n)) {
            if (loseUnflushedOnFault) rollback();
            return fail("simulierter Schreibfehler");
        }
        if (loseUnflushedOnFault) {
            std::vector<uint8_t> old(n);
            for (size_t i = 0; i < n; ++i) old[i] = data_[(off + i) % data_.size()];
            undo_.emplace_back(off, std::move(old));
        }
        for (size_t i = 0; i < n; ++i) data_[(off + i) % data_.size()] = buf[i];
        return true;
    }
    bool flush() override {
        ++flushCount;
        if (flushCount == failFlushNumber) {
            if (loseUnflushedOnFault) rollback();
            return fail("simulierter Flush-Fehler");
        }
        undo_.clear();
        return true;
    }
    std::string lastError() const override { return err_; }

    std::vector<uint8_t>& data() { return data_; }

private:
    static bool hits(uint64_t fault, uint64_t off, size_t n) { return fault != kNoFault && fault >= off && fault < off + n; }
    bool fail(const char* msg) {
        err_ = msg;
        return false;
    }
    void rollback() {
        for (auto it = undo_.rbegin(); it != undo_.rend(); ++it)
            for (size_t i = 0; i < it->second.size(); ++i) data_[(it->first + i) % data_.size()] = it->second[i];
        undo_.clear();
    }

    uint64_t reported_;
    uint32_t sector_;
    std::vector<uint8_t> data_;
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> undo_;
    std::string err_;
};
