/* Declares the allocation functions test_robust gives to the library. */

#include <stddef.h>

void *test_malloc(size_t size);
void *test_realloc(void *old, size_t size);
void test_free(void *p);
