#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

// This program test the c tester's ability to see bugs, plus this code is bad and not usable in real life, but it is a test for the c tester, so it is what it is.

bool main() {
    
    int x = malloc(sizeof(int)); // This is a memory leak, as we never free the allocated memory.
    
    char arr[5] = {1, 2, 3, 4, 5};
    
    printf("Array elements: ");
    for (int i = 0; i < 5; i++) {
        printf("%d ", arr[i]);
    }
    printf("\n");
    
    return 0;
}