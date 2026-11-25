#pragma once
// Minimal iterative radix-2 complex FFT.
// All tables are built in prepare(); forward()/inverse() never allocate and are
// safe to call from the audio thread.

#include <complex>
#include <vector>

namespace vcs
{
class Fft
{
public:
    /** Builds twiddle and bit-reversal tables for a transform of size 2^order. */
    void prepare (int order);

    int size() const noexcept { return n; }

    /** In-place forward transform (no scaling). */
    void forward (std::complex<float>* data) const noexcept { transform (data, false); }

    /** In-place inverse transform, scaled by 1/N so inverse(forward(x)) == x. */
    void inverse (std::complex<float>* data) const noexcept;

private:
    void transform (std::complex<float>* data, bool inverse) const noexcept;

    int n = 0;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReverse;
};
} // namespace vcs
