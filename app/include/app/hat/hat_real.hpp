#ifndef MTS_APP_HAT_HAT_REAL_HPP
#define MTS_APP_HAT_HAT_REAL_HPP

#include <app/hat/hat.hpp>

#include <RTIMULib.h>

#include <cstdint>
#include <memory>

namespace mts
{

class Hat_Real final : public Hat
{
public:
  Hat_Real(const Hat_Config& cfg, RTIMU* imu);

  Hat_Status read_sample(Raw_Sample& out) override;
  void show(const Led_Command& cmd) override;
  Hat_Info info() const override;

  ~Hat_Real() override;

private:
  Hat_Config cfg_;
  RTIMU* imu_;

  uint64_t seq_;
  int64_t last_t_ns_;
  double measured_rate_hz_;
  float last_mag_[3];
  Led_Command led_;
};

std::unique_ptr<Hat> hat_real_open(const Hat_Config& cfg);

} // namespace mts

#endif