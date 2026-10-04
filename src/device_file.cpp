#include "device_file.h"

#include <stdexcept>

namespace dw {

FileDevice::FileDevice(const std::string& path, uint32_t sectorSize) : sector_(sectorSize) {
    file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_) throw std::runtime_error("Datei kann nicht geöffnet werden: " + path);
    file_.seekg(0, std::ios::end);
    size_ = static_cast<uint64_t>(file_.tellg());
}

bool FileDevice::inRange(uint64_t offset, size_t len) {
    if (offset > size_ || len > size_ - offset) {
        err_ = "Zugriff außerhalb des Datenträgers";
        return false;
    }
    return true;
}

bool FileDevice::read(uint64_t offset, uint8_t* buf, size_t len) {
    if (!inRange(offset, len)) return false;
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
    if (!file_) {
        err_ = "Lesen aus Datei fehlgeschlagen";
        return false;
    }
    return true;
}

bool FileDevice::write(uint64_t offset, const uint8_t* buf, size_t len) {
    if (!inRange(offset, len)) return false;
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset));
    file_.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(len));
    if (!file_) {
        err_ = "Schreiben in Datei fehlgeschlagen";
        return false;
    }
    return true;
}

bool FileDevice::flush() {
    file_.flush();
    if (!file_) {
        err_ = "Flush der Datei fehlgeschlagen";
        return false;
    }
    return true;
}

}  // namespace dw
