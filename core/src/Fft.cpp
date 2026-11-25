#include "vcs/Fft.h"

#include <cassert>
#include <cmath>
#include <utility>

namespace vcs
{
void Fft::prepare (int order)
{
    assert (order >= 1 && order <= 20);
    n = 1 << order;

    twiddles.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
    {
        const double phase = -2.0 * M_PI * k / n;
        twiddles[(size_t) k] = { (float) std::cos (phase), (float) std::sin (phase) };
    }

    bitReverse.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitReverse[(size_t) i] = r;
    }
}

void Fft::inverse (std::complex<float>* data) const noexcept
{
    transform (data, true);
    const float scale = 1.0f / (float) n;
    for (int i = 0; i < n; ++i)
        data[i] *= scale;
}

void Fft::transform (std::complex<float>* data, bool inv) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int r = bitReverse[(size_t) i];
        if (r > i)
            std::swap (data[i], data[r]);
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int start = 0; start < n; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                auto w = twiddles[(size_t) (k * step)];
                if (inv)
                    w = std::conj (w);
                const auto a = data[start + k];
                const auto b = data[start + k + half] * w;
                data[start + k] = a + b;
                data[start + k + half] = a - b;
            }
        }
    }
}
} // namespace vcs
