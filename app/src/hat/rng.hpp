/* ===================================================================================================
 * app/src/hat/rng.hpp
 *
 * Deterministic random numbers for Hat_Sim (private to the hat module).
 *
 * Same seed, same numbers, on every machine, bit for bit:
 *  - generator: xoshiro256** (Blackman, Vigna), state seeded by splitmix64.
 *    Integer only, fully specified.
 *  - gaussian: sum of 12 uniforms minus 6 (Irwin-Hall). Mean 0, variance
 *    exactly 1, cut off at +-6 sigma: plenty for sensor noise. Chosen over
 *    Box-Muller because that needs log/sqrt/cos, which glibc and Apple's
 *    libm may round differently in the last bit. This uses only additions
 *    and an exact multiplication by a power of two.
 *
 * std::normal_distribution is not used for the same reason: the standard
 * does not specify its algorithm, libstdc++ and libc++ give different data.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_HAT_RNG_HPP
#define MTS_APP_HAT_RNG_HPP

#include <cstdint>

namespace mts
{

class Rng
{
public:
  explicit Rng(uint64_t seed)
  {
    uint64_t x = seed;
    for (uint64_t& word : s_)
    {
      word = splitmix64(x);
    }
  }

  // Next 64 random bits (xoshiro256**)
  uint64_t next()
  {
    const uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;

    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);

    return result;
  }

  // Uniform in [0, 1): the top 53 bits, times 2^-53 (exact)
  double uniform()
  {
    return static_cast<double>(next() >> 11) * 0x1.0p-53;
  }

  // Approximately standard normal: mean 0, variance 1, range [-6, 6]
  double gaussian()
  {
    double sum = 0.0;
    for (int i = 0; i < 12; i++)
    {
      sum += uniform();
    }
    return sum - 6.0;
  }

private:
  static uint64_t rotl(uint64_t x, int k)
  {
    return (x << k) | (x >> (64 - k));
  }

  // Spreads one seed over the 4 state words (and never yields all zeros)
  static uint64_t splitmix64(uint64_t& x)
  {
    x += 0x9E3779B97F4A7C15ULL;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  uint64_t s_[4];
};

} // namespace mts

#endif /* MTS_APP_HAT_RNG_HPP */

