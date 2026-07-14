/*
 * TestRegistry — a SINGLETON that owns the list of all known tests,
 * each stored with a FACTORY that can create a fresh instance of it.
 *
 * Why a factory (and not the test objects themselves)? Every run gets
 * a brand-new test object, so no state leaks between runs.
 *
 * Why a singleton? Registration happens from many .cpp files during
 * static initialization, before main() starts — they all need to find
 * the one shared registry, and a Meyers singleton (static local in
 * instance()) is guaranteed to be constructed before first use.
 *
 * The FTL_TEST macro at the bottom is what test files actually use:
 * it declares a subclass, plants a static Registrar whose constructor
 * runs before main() and registers the factory, then opens the body
 * of run() for you to write the test into.
 */
#pragma once

#include "test_case.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace framework {

using TestFactory = std::function<std::unique_ptr<TestCase>()>;

struct TestInfo {
    std::string suite;
    std::string name;
    TestFactory factory;
};

class TestRegistry {
public:
    static TestRegistry& instance();               // the ONLY way in

    void add(const std::string& suite, const std::string& name,
             TestFactory factory);
    const std::vector<TestInfo>& tests() const { return tests_; }

    TestRegistry(const TestRegistry&) = delete;    // no copies of a
    TestRegistry& operator=(const TestRegistry&) = delete;  // singleton

private:
    TestRegistry() = default;                      // private ctor
    std::vector<TestInfo> tests_;
};

/* One static Registrar per test does the self-registration. */
struct Registrar {
    Registrar(const char* suite, const char* name, TestFactory factory) {
        TestRegistry::instance().add(suite, name, std::move(factory));
    }
};

} // namespace framework

/* Declares a test deriving from `Fixture`, registers it under
 * Suite/Name, and opens the body of its run() method. */
#define FTL_TEST(Suite, Name, Fixture)                                   \
    class Name##_Test : public Fixture {                                 \
    public:                                                              \
        void run() override;                                             \
    };                                                                   \
    static framework::Registrar Name##_registrar(                        \
        #Suite, #Name,                                                   \
        [] {                                                             \
            return std::unique_ptr<framework::TestCase>(new Name##_Test());\
        });                                                              \
    void Name##_Test::run()
