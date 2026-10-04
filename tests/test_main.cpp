#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "test.h"

std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

std::string tmpPath(const std::string& name) { return "tests/tmp/" + name; }

void writeFile(const std::string& path, uint64_t size, uint8_t fill) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    std::vector<char> chunk(1 << 16, static_cast<char>(fill));
    for (uint64_t left = size; left > 0;) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(left, chunk.size()));
        f.write(chunk.data(), static_cast<std::streamsize>(n));
        left -= n;
    }
    if (!f) throw TestFailure("writeFile fehlgeschlagen: " + path);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw TestFailure("readFile fehlgeschlagen: " + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const TestCase& t : testRegistry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        ++run;
        try {
            t.fn();
            std::printf("PASS %s\n", t.name);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("FAIL %s\n  %s\n", t.name, e.what());
        }
    }
    std::printf("\n%d/%d Tests bestanden\n", run - failed, run);
    return failed == 0 && run > 0 ? 0 : 1;
}
