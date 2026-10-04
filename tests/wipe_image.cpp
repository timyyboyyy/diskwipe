// Testwerkzeug (wird nicht ausgeliefert): wendet den Löschablauf auf eine Image-Datei an.
// Aufruf: wipe_image <image> <zufallsdurchgaenge>  |  wipe_image --verify <image>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "device_file.h"
#include "wiper.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Aufruf: wipe_image <image> <zufallsdurchgaenge> | wipe_image --verify <image>\n");
        return 2;
    }
    try {
        const bool verify = std::strcmp(argv[1], "--verify") == 0;
        dw::FileDevice dev(verify ? argv[2] : argv[1]);
        std::atomic<bool> cancel{false};
        const dw::Result r = verify ? dw::runVerifyZero(dev, nullptr, cancel)
                                    : dw::runWipe(dev, std::atoi(argv[2]), nullptr, cancel);
        if (r.status == dw::Status::Success) {
            std::printf("OK: %llu Bytes = 0x00\n", static_cast<unsigned long long>(r.bytesTotal));
            return 0;
        }
        std::printf("FEHLER (Durchgang %d): %s\n", r.failedPass, r.message.c_str());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FEHLER: %s\n", e.what());
        return 1;
    }
}
