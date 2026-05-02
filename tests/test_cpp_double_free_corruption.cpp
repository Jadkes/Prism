/*
 * test_cpp_double_free_corruption.cpp - Test: double free or corruption
 *
 * Purpose: Verify c_tester detects heap corruption from double-free.
 * Pattern: "double free or corruption" -> ERR_DOUBLE_FREE_CPP
 * Expected: "[ERROR] Double Free or Corruption" with fix suggestion.
 */
#include <cstdlib>

int main()
{
    void *p = std::malloc(100);
    if (!p) return 1;

    /* First free */
    std::free(p);

    /* Second free - corrupts heap */
    std::free(p);

    return 0;
}
