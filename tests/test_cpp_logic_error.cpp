/*
 * test_cpp_logic_error.cpp - Test: std::logic_error exception
 *
 * Purpose: Verify c_tester detects logic errors.
 * Pattern: "std::logic_error" -> ERR_LOGIC_ERROR
 * Expected: "[ERROR] Logic Error" with fix suggestion.
 */
#include <stdexcept>
#include <string>

int main()
{
    /* Throw a logic error - simulates violated precondition */
    try {
        throw std::logic_error("Logical precondition violated");
    } catch (const std::logic_error& e) {
        /* Expected exception - won't be caught by c_tester */
        return 0;
    }

    return 1;  /* Should not reach here */
}
