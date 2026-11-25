// File name is ours; stock name unknown.
// Minimal assert-style runner for the host tests: each test file is one
// executable; CHECK failures are counted and printed, TEST_RESULT() is main's
// return value. No framework.
#ifndef RCD330_HMI_TESTS_TEST_RUNNER_H
#define RCD330_HMI_TESTS_TEST_RUNNER_H

#include <stdio.h>
#include <string.h>

static int g_testFailures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_testFailures;                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_STREQ(actual, expected)                                            \
    do {                                                                         \
        const char* a_ = (actual);                                               \
        const char* e_ = (expected);                                             \
        if (a_ == 0 || strcmp(a_, e_) != 0) {                                    \
            ++g_testFailures;                                                    \
            fprintf(stderr, "%s:%d: CHECK_STREQ(%s): got \"%s\", want \"%s\"\n", \
                    __FILE__, __LINE__, #actual, a_ ? a_ : "(null)", e_);        \
        }                                                                        \
    } while (0)

// Integer comparison; both sides are printed on failure.
#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        const long long a_ = (long long)(actual);                                \
        const long long e_ = (long long)(expected);                              \
        if (a_ != e_) {                                                          \
            ++g_testFailures;                                                    \
            fprintf(stderr, "%s:%d: CHECK_EQ(%s, %s): got %lld (0x%llx), want %lld (0x%llx)\n", \
                    __FILE__, __LINE__, #actual, #expected, a_, (unsigned long long)a_, e_, \
                    (unsigned long long)e_);                                     \
        }                                                                        \
    } while (0)

#define TEST_RESULT() (g_testFailures == 0 ? 0 : 1)

#define RUN_TEST(fn) fn()

static inline int test_summary() {
    return TEST_RESULT();
}

#endif
