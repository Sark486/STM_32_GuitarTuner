#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>

// Shared by the host tests. Each test is a single .c file, so every test program gets its own counter;
// main() returns non-zero when it is not 0.
static uint16_t failure_count = 0U;

// counts a failure and prints the description when result is false
static inline void test_check(bool result, const char *description)
{
  if (!result)
  {
    failure_count++;
    printf("FAIL: %s\n", description);
  }
}

#endif /* TEST_UTIL_H */
