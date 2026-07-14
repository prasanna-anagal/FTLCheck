#include "test_registry.h"

namespace framework {

/* Meyers singleton: the static local is constructed the first time
 * this function runs, and C++ guarantees that happens exactly once
 * (even if called from several translation units' static init). */
TestRegistry& TestRegistry::instance() {
    static TestRegistry registry;
    return registry;
}

void TestRegistry::add(const std::string& suite, const std::string& name,
                       TestFactory factory) {
    tests_.push_back(TestInfo{suite, name, std::move(factory)});
}

} // namespace framework
