/* ===================================================================================================
 * app/src/hat/hat.cc
 *
 * hat_open(): validates the config, then builds the implementation the
 * mode asks for. The only place that knows which implementations exist.
 *
 * Hat_Real (MTS_WITH_RTIMU builds only) provides, in its private
 * hat_real.hpp:
 *   std::unique_ptr<Hat> hat_real_open(const Hat_Config& cfg);
 * returning nullptr (logged) if the hardware cannot be opened: a
 * constructor cannot fail without exceptions, a function can.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include "hat_sim.hpp"

#if defined(MTS_WITH_RTIMU)
#include <app/hat/hat_real.hpp>
#endif

#include <app/hat/hat.hpp>
#include <utils/logger/logger.h>

#include <memory>

namespace mts
{

std::unique_ptr<Hat> hat_open(const Hat_Config& cfg)
{
  // !(x > 0) also rejects NaN
  if (!(cfg.sample_rate_hz > 0.0) || cfg.sample_rate_hz > HAT_MAX_RATE_HZ)
  {
    MTS_LOG_ERROR("hat_open: sample_rate_hz %g out of range (0, %g]", cfg.sample_rate_hz, HAT_MAX_RATE_HZ);
    return nullptr;
  }

  if (!hat_accel_range_supported(cfg.accel_range_g))
  {
    MTS_LOG_ERROR("hat_open: accel_range_g %u not supported (2, 4, 8, 16)", cfg.accel_range_g);
    return nullptr;
  }

  if (!hat_gyro_range_supported(cfg.gyro_range_dps))
  {
    MTS_LOG_ERROR("hat_open: gyro_range_dps %u not supported (245, 500, 2000)", cfg.gyro_range_dps);
    return nullptr;
  }

  switch (cfg.mode)
  {
    case Hat_Mode::sim:
    {
      MTS_LOG_INFO("hat_open: simulated hat, %g Hz, seed %llu", cfg.sample_rate_hz,
                   static_cast<unsigned long long>(cfg.sim.seed));
      return std::make_unique<Hat_Sim>(cfg);
    }

    case Hat_Mode::real:
    {
#if defined(MTS_WITH_RTIMU)
      return hat_real_open(cfg);
#else
      MTS_LOG_ERROR("hat_open: real hat requested but this build has no RTIMULib (configure with -DMTS_WITH_RTIMU=ON)");
      return nullptr;
#endif
    }
  }

  MTS_LOG_ERROR("hat_open: unknown mode %d", static_cast<int>(cfg.mode));
  return nullptr;
}

bool hat_real_available()
{
#if defined(MTS_WITH_RTIMU)
  return true;
#else
  return false;
#endif
}
} // namespace mts

