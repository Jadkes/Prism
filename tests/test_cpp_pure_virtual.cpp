/*
 * test_cpp_pure_virtual.cpp - Test: pure virtual method called
 *
 * Purpose: Verify c_tester detects pure virtual function calls.
 * Pattern: "pure virtual method called" -> ERR_PURE_VIRTUAL
 * Expected: "[ERROR] Pure Virtual Method Called" with fix suggestion.
 * Note: This is very difficult to trigger portably.
 *       Most implementations won't call pure virtual after destruction.
 */
#include <iostream>

class Base {
public:
    Base() { }
    virtual ~Base() {
        /* Calling virtual from destructor */
        foo();
    }
    virtual void foo() = 0;  /* Pure virtual */
};

class Derived : public Base {
public:
    Derived() : Base() { }
    void foo() override {
        /* Do nothing */
    }
};

int main()
{
    /* This may or may not trigger pure virtual at runtime */
    Derived d;
    /* d is destroyed here, Base destructor calls foo() */

    return 0;
}
