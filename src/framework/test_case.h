/*
 * TestCase — the abstract base class every test derives from.
 *
 * This is the OOP backbone of the framework:
 *   - ABSTRACTION:  the runner works only with this interface; it has
 *                   no idea what any concrete test actually does.
 *   - INHERITANCE:  fixtures (like FtlFixture) derive from TestCase,
 *                   and concrete tests derive from the fixture.
 *   - POLYMORPHISM: the runner calls run() through a base-class
 *                   pointer and dynamic dispatch finds the right body.
 *   - The virtual destructor matters: tests are deleted through a
 *     TestCase* — without it, derived destructors would never run.
 */
#pragma once

namespace framework {

class TestCase {
public:
    virtual ~TestCase() = default;

    /* Overridable lifecycle hooks (Template Method pattern):
     * the runner always calls setUp -> run -> tearDown. */
    virtual void setUp() {}
    virtual void tearDown() {}

    /* Pure virtual: a TestCase is abstract, only concrete tests
     * can be instantiated. */
    virtual void run() = 0;
};

} // namespace framework
