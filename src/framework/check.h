/*
 * Assertion macros for test cases.
 *
 * A failed REQUIRE throws CheckFailure; the TestRunner catches it,
 * marks the test failed, and keeps running the remaining tests. The
 * exception message carries the expression text and the exact
 * file:line, so a failure report reads like a compiler error.
 */
#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

namespace framework {

class CheckFailure : public std::runtime_error {
public:
    CheckFailure(const std::string& what, const char* file, int line)
        : std::runtime_error(build(what, file, line)) {}

private:
    static std::string build(const std::string& what,
                             const char* file, int line) {
        std::ostringstream os;
        os << "REQUIRE failed: " << what << "  [" << file << ":" << line << "]";
        return os.str();
    }
};

} // namespace framework

#define REQUIRE(cond)                                                    \
    do {                                                                 \
        if (!(cond))                                                     \
            throw framework::CheckFailure(#cond, __FILE__, __LINE__);    \
    } while (0)

#define REQUIRE_EQ(a, b)                                                 \
    do {                                                                 \
        auto va_ = (a);                                                  \
        auto vb_ = (b);                                                  \
        if (!(va_ == vb_)) {                                             \
            std::ostringstream os_;                                      \
            os_ << #a " == " #b "  (got " << va_ << " vs " << vb_ << ")";\
            throw framework::CheckFailure(os_.str(), __FILE__, __LINE__);\
        }                                                                \
    } while (0)

#define REQUIRE_THROWS_AS(expr, ExType)                                  \
    do {                                                                 \
        bool caught_ = false;                                            \
        try { expr; } catch (const ExType&) { caught_ = true; }          \
        if (!caught_)                                                    \
            throw framework::CheckFailure(                               \
                "expected " #ExType " to be thrown by: " #expr,          \
                __FILE__, __LINE__);                                     \
    } while (0)
