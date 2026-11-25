#include "TestFramework.h"

#include <atomic>
#include <cstdlib>
#include <new>

// Global allocation counter: lets tests prove the audio path never allocates.
namespace vcstest
{
std::atomic<long> gAllocations { 0 };
}

void* operator new (std::size_t size)
{
    vcstest::gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& t : vcstest::registry())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos)
            continue;
        const int before = vcstest::failures();
        std::printf ("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf ("%s %s\n", vcstest::failures() == before ? "[  OK  ]" : "[ FAIL ]", t.name);
        ++run;
    }
    std::printf ("\n%d tests, %d failed checks\n", run, vcstest::failures());
    return vcstest::failures() == 0 ? 0 : 1;
}
