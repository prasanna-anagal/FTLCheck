#include "test_runner.h"
#include "check.h"

#include <chrono>
#include <cstdio>

namespace framework {

TestRunner& TestRunner::instance() {
    static TestRunner runner;
    return runner;
}

std::vector<TestResult> TestRunner::run(const std::string& suiteFilter,
                                        const std::string& nameFilter) {
    std::vector<TestResult> results;

    for (const TestInfo& info : TestRegistry::instance().tests()) {
        if (!suiteFilter.empty() && info.suite != suiteFilter) continue;
        if (!nameFilter.empty() && info.name != nameFilter) continue;

        TestResult r;
        r.suite = info.suite;
        r.name  = info.name;

        /* fresh instance per run — no state leaks between tests */
        std::unique_ptr<TestCase> test = info.factory();

        auto start = std::chrono::steady_clock::now();
        try {
            test->setUp();
            test->run();          // dynamic dispatch to the actual test
            r.passed = true;
        } catch (const CheckFailure& f) {
            r.message = f.what();
        } catch (const std::exception& e) {
            r.message = std::string("unexpected exception: ") + e.what();
        } catch (...) {
            r.message = "unknown exception";
        }
        try { test->tearDown(); } catch (...) { /* never mask the result */ }
        auto end = std::chrono::steady_clock::now();

        r.millis = std::chrono::duration<double, std::milli>(end - start).count();

        std::printf("  [%s] %s.%s (%.1f ms)\n",
                    r.passed ? "PASS" : "FAIL",
                    r.suite.c_str(), r.name.c_str(), r.millis);
        if (!r.passed)
            std::printf("         %s\n", r.message.c_str());

        results.push_back(std::move(r));
    }
    return results;
}

} // namespace framework
