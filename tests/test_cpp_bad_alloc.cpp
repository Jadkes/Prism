/*
 * test_cpp_bad_alloc.cpp - Test: std::bad_alloc exception
 *
 * Purpose: Verify c_tester detects memory allocation failures.
 * Pattern: "std::bad_alloc" -> ERR_BAD_ALLOC
 * Expected: "[ERROR] Bad Allocation" with fix suggestion.
 */
#include <new>
#include <iostream>

int main()
{
    /* Try to allocate an impossible amount of memory */
    try {
        /* Request more memory than system can provide */
        char *p = new char[(size_t)-1];
        (void)p;
    } catch (const std::bad_alloc& e) {
        /* Expected exception - won't be caught by c_tester */
        return 0;
    }

    return 1;  /* Should not reach here */
}
