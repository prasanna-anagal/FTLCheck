/*
 * TestRunner — a SINGLETON that executes registered tests.
 *
 * For each selected test it: builds a fresh instance via the factory,
 * calls setUp() -> run() -> tearDown(), times it, and converts any
 * thrown CheckFailure (assertion) or other exception into a recorded
 * failure instead of crashing the whole run.
 */
#pragma once

#include "test_registry.h"

#include <string>
#include <vector>

namespace framework {

struct TestResult {
    std::string suite;
    std::string name;
    bool passed = false;
    std::string message;   // empty when passed; failure text otherwise
    double millis = 0.0;
};

class TestRunner {
public:
    static TestRunner& instance();

    void setVerbose(bool v) { verbose_ = v; }

    /* Run every registered test matching the filters (empty filter
     * matches everything). Prints progress; returns all results. */
    std::vector<TestResult> run(const std::string& suiteFilter = "",
                                const std::string& nameFilter = "");

    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

private:
    TestRunner() = default;
    bool verbose_ = false;
};

} // namespace framework
