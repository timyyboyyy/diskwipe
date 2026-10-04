#pragma once
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& testRegistry();

struct TestRegistrar {
    TestRegistrar(const char* name, void (*fn)()) { testRegistry().push_back({name, fn}); }
};

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define TEST(name)                                                  \
    static void name();                                             \
    static TestRegistrar name##_registrar(#name, name);             \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::ostringstream os_;                                                   \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") fehlgeschlagen"; \
            throw TestFailure(os_.str());                                             \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        auto a_ = (a);                                                                   \
        auto b_ = (b);                                                                   \
        if (!(a_ == b_)) {                                                               \
            std::ostringstream os_;                                                      \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a ", " #b ") fehlgeschlagen: " \
                << a_ << " != " << b_;                                                   \
            throw TestFailure(os_.str());                                                \
        }                                                                                \
    } while (0)

// Pfad unterhalb von tests/tmp (wird vom Makefile angelegt).
std::string tmpPath(const std::string& name);
void writeFile(const std::string& path, uint64_t size, uint8_t fill);
std::vector<uint8_t> readFile(const std::string& path);
