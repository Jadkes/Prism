/*
 * test_cpp_out_of_range.cpp - Test: std::out_of_range exception
 *
 * Purpose: Verify c_tester detects out-of-range container access.
 * Pattern: "std::out_of_range" -> ERR_OUT_OF_RANGE
 * Expected: "[ERROR] Out of Range" with fix suggestion.
 */
#include <vector>
#include <stdexcept>

int main()
{
    std::vector<int> v;
    v.push_back(42);

    /* Access out of bounds - throws std::out_of_range */
    try {
        int value = v.at(5);  /* Invalid index */
        (void)value;
    } catch (const std::out_of_range& e) {
        /* Expected exception - won't be caught by c_tester */
        return 0;
    }

    return 1;  /* Should not reach here */
}
