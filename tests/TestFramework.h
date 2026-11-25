#pragma once
// Tiny self-contained test framework (no external dependency).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace vcstest
{
struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

inline int& failures()
{
    static int f = 0;
    return f;
}

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
};
} // namespace vcstest

#define VCS_CONCAT2(a, b) a##b
#define VCS_CONCAT(a, b) VCS_CONCAT2 (a, b)

#define TEST_CASE(name)                                                                       \
    static void VCS_CONCAT (test_fn_, __LINE__)();                                            \
    static vcstest::Registrar VCS_CONCAT (test_reg_, __LINE__) (name, VCS_CONCAT (test_fn_, __LINE__)); \
    static void VCS_CONCAT (test_fn_, __LINE__)()

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (! (cond)) {                                                            \
            std::printf ("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            ++vcstest::failures();                                                 \
        }                                                                          \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                          \
    do {                                                                                               \
        const double va_ = (double) (a), vb_ = (double) (b);                                           \
        if (! (std::abs (va_ - vb_) <= (double) (tol))) {                                              \
            std::printf ("    FAILED %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, va_, vb_, (double) (tol)); \
            ++vcstest::failures();                                                                     \
        }                                                                                              \
    } while (0)

#define INFO(...) std::printf ("    " __VA_ARGS__), std::printf ("\n")
