
#include <app/processing/processing.hpp>

#include <core/detect/detect.h>
#include <core/fusion/fusion.h>
#include <core/separation/separation.h>
#include <core/types/units.h>

#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstring>
#include <exception>
#include <memory>

namespace mts
{

// --------------------------------------------------
// LSTM forecast -> processed displacement / rotation
// --------------------------------------------------

static bool project_lstm_via_filters(
    const core::onnx::TremorForecast& forecast,
    const Raw_Sample& current_raw,
    const Calibration& calibration,
    const Fusion* live_fusion,
    const Separation* live_translation,
    const Separation* live_rotation,
    Fusion* predicted_fusion,
    Separation* predicted_translation,
    Separation* predicted_rotation,
    double sample_rate_hz,
    uint32_t horizon,
    double result[6])
{
  if (horizon == 0 || horizon > 5 ||
      !std::isfinite(sample_rate_hz) ||
      sample_rate_hz <= 0.0 ||
      !live_fusion ||
      !live_translation ||
      !live_rotation ||
      !predicted_fusion ||
      !predicted_translation ||
      !predicted_rotation)
  {
    return false;
  }

  // Copy the warmed-up live filter states.
  // Do NOT update the real filters with predictions.

  if (!fusion_copy_state(
        predicted_fusion, live_fusion) ||
      !separation_copy_state(
        predicted_translation, live_translation) ||
      !separation_copy_state(
        predicted_rotation, live_rotation))
  {
    return false;
  }

  // Process predicted future IMU samples
  for (uint32_t t = 0; t < horizon; ++t)
  {
    Raw_Sample predicted = current_raw;

    predicted.seq = current_raw.seq + t + 1;

    predicted.t_ns =
      current_raw.t_ns +
      static_cast<int64_t>(
        std::llround(
          (t + 1) * 1e9 / sample_rate_hz
        )
      );

    // No predicted magnetometer measurements
    predicted.flags &=
      ~(SAMPLE_MAG_FRESH | SAMPLE_GAP | SAMPLE_SATURATED);

    for (int a = 0; a < 3; ++a)
    {
      // Acceleration: g -> m/s²
      predicted.accel[a] =
        static_cast<float>(
          forecast[t][a] *
          MTS_STANDARD_GRAVITY_MS2
        );

      // Gyroscope: already rad/s
      predicted.gyro[a] =
        static_cast<float>(
          forecast[t][a + 3]
        );

      if (!std::isfinite(predicted.accel[a]) ||
          !std::isfinite(predicted.gyro[a]))
      {
        return false;
      }
    }

    // Apply the same calibration as real samples
    Raw_Sample calibrated{};
    calibration_apply(
      &calibration,
      &predicted,
      &calibrated
    );

    // Gravity removal and orientation estimation
    Fusion_Output fused{};
    fusion_update(
      predicted_fusion,
      &calibrated,
      &fused
    );

    // Translation: acceleration -> displacement
    Separation_Output trans{};
    separation_update(
      predicted_translation,
      &fused.lin_acc_body,
      &trans
    );

    // Rotation: angular velocity -> angle
    const Vec3 gyro = {{
      calibrated.gyro[0],
      calibrated.gyro[1],
      calibrated.gyro[2]
    }};

    Separation_Output rot{};
    separation_update(
      predicted_rotation,
      &gyro,
      &rot
    );

    // Save result at requested prediction horizon
    if (t + 1 == horizon)
    {
      for (int a = 0; a < 3; ++a)
      {
        // Metres
        result[a] = trans.tremor_disp.v[a];

        // Radians
        result[a + 3] = rot.tremor_disp.v[a];

        if (!std::isfinite(result[a]) ||
            !std::isfinite(result[a + 3]))
        {
          return false;
        }
      }
    }
  }

  return true;
}


// --------------------------------------------------
// Processing_Core constructor
// --------------------------------------------------

Processing_Core::Processing_Core(double rate_hz)
  : rate_hz_(rate_hz)
  , cal_{}
  , fusion_(nullptr)
  , separation_(nullptr)
  , detect_(nullptr)
  , separation_rot_(nullptr)
  , detect_rot_(nullptr)
  , cfg_gen_(0)
  , horizon_(0)
  , active_(PREDICTOR_COUNT)
  , n_slots_(0)
  , slots_(new Slot[P_MAX])
  , since_reset_(0)
{
  calibration_identity(&cal_);

  const Fusion_Config fc =
    fusion_config_default(rate_hz);

  const Separation_Config sc =
    separation_config_default(rate_hz);

  const Detect_Config dc =
    detect_config_default(rate_hz);

  // Live translation pipeline
  fusion_ = fusion_create(&fc);
  separation_ = separation_create(&sc);
  detect_ = detect_create(&dc);

  // Independent prediction pipeline
  fusion_predict_ = fusion_create(&fc);
  separation_predict_ = separation_create(&sc);

  // Live rotation pipeline
  Separation_Config sr = sc;
  sr.integrations = 1;

  Detect_Config dr = dc;
  dr.min_amp = ROTATION_MIN_AMP_RAD;

  separation_rot_ = separation_create(&sr);
  detect_rot_ = detect_create(&dr);

  // Independent rotation prediction pipeline
  separation_rot_predict_ = separation_create(&sr);

  // Load ONNX model once
  try
  {
    lstm_ =
      std::make_unique<core::onnx::OnnxPredictor>(
        "models/tremor_lstm.onnx"
      );

    MTS_LOG_INFO("LSTM model loaded successfully");
  }
  catch (const std::exception& e)
  {
    MTS_LOG_ERROR(
      "LSTM loading failed: %s",
      e.what()
    );
  }
}


// --------------------------------------------------
// Destructor
// --------------------------------------------------

Processing_Core::~Processing_Core()
{
  fusion_destroy(fusion_);
  separation_destroy(separation_);
  detect_destroy(detect_);

  separation_destroy(separation_rot_);
  detect_destroy(detect_rot_);

  fusion_destroy(fusion_predict_);
  separation_destroy(separation_predict_);
  separation_destroy(separation_rot_predict_);
}


bool Processing_Core::valid() const
{
  return fusion_ &&
         separation_ &&
         detect_ &&
         separation_rot_ &&
         detect_rot_;
}


uint32_t Processing_Core::horizon_steps() const
{
  return horizon_;
}


// --------------------------------------------------
// Apply configuration
// --------------------------------------------------

void Processing_Core::apply(
    const Pipeline_Control& ctl)
{
  if (ctl.cfg_gen == cfg_gen_)
  {
    return;
  }

  cfg_gen_ = ctl.cfg_gen;

  const Runtime_Settings& rs = ctl.settings;

  const double steps =
    std::llround(
      (double)rs.horizon_ns * rate_hz_ / 1e9
    );

  horizon_ =
    steps < 1.0 ? 1 :
    (steps > K_MAX ? K_MAX : (uint32_t)steps);

  active_ = rs.active_predictor_id;

  n_slots_ = 0;

  for (Predictor_Id id = 0;
       id < PREDICTOR_COUNT && n_slots_ < P_MAX;
       id++)
  {
    if (!(rs.enabled_mask & (UINT32_C(1) << id)))
    {
      continue;
    }

    Slot& slot = slots_[n_slots_++];

    slot.id = id;
    slot.ops = predictor_by_id(id);

    // LSTM uses ONNX rather than scalar Predictor_Ops
    if (id == PREDICTOR_LSTM)
    {
      slot.ops = nullptr;
      continue;
    }

    const Predictor_Config pc =
      predictor_config_default(
        id, rate_hz_, horizon_
      );

    for (int a = 0; a < AXES && slot.ops; ++a)
    {
      if (!slot.ops->init(slot.state[a], &pc))
      {
        slot.ops = nullptr;
      }
    }
  }

  reset_predictors();
}


// --------------------------------------------------
// Reset predictors
// --------------------------------------------------

void Processing_Core::reset_predictors()
{
  for (uint32_t i = 0; i < n_slots_; ++i)
  {
    Slot& slot = slots_[i];

    for (int a = 0; a < AXES && slot.ops; ++a)
    {
      slot.ops->reset(slot.state[a]);
    }

    std::memset(
      slot.made_valid,
      0,
      sizeof(slot.made_valid)
    );
  }

  since_reset_ = 0;
  lstm_samples_ = 0;
}


// --------------------------------------------------
// Process one sensor sample
// --------------------------------------------------

void Processing_Core::process(
    const Raw_Sample& raw,
    int64_t now_ns,
    Output_Frame& out)
{
  std::memset(&out, 0, sizeof(out));

  out.seq = raw.seq;
  out.t_ns = raw.t_ns;

  // Original raw sensor measurements
  for (int a = 0; a < 3; ++a)
  {
    out.imu_acc.v[a] = raw.accel[a];
    out.imu_gyro.v[a] = raw.gyro[a];
  }

  // ------------------------------------------------
  // Stage 1: Real sensor processing
  // ------------------------------------------------

  Raw_Sample s{};
  calibration_apply(&cal_, &raw, &s);

  Fusion_Output fo{};
  fusion_update(fusion_, &s, &fo);

  Separation_Output so{};
  separation_update(
    separation_,
    &fo.lin_acc_body,
    &so
  );

  Detect_Output d{};
  detect_update(detect_, &so, &d);

  // Rotation
  const Vec3 gyro = {{
    s.gyro[0], s.gyro[1], s.gyro[2]
  }};

  Separation_Output sr{};
  separation_update(
    separation_rot_,
    &gyro,
    &sr
  );

  Detect_Output dr{};
  detect_update(detect_rot_, &sr, &dr);

  // Handle missing sensor samples
  if (raw.flags & SAMPLE_GAP)
  {
    out.flags |= FRAME_GAP_BEFORE;
    reset_predictors();
  }

  // ------------------------------------------------
  // Stage 2: Build LSTM input window
  // ------------------------------------------------

  if (lstm_)
  {
    if (lstm_samples_ == 30)
    {
      for (std::size_t t = 1; t < 30; ++t)
      {
        lstm_window_[t - 1] = lstm_window_[t];
      }
    }

    const std::size_t index =
      lstm_samples_ < 30 ? lstm_samples_ : 29;

    for (int a = 0; a < 3; ++a)
    {
      // Raw acceleration m/s² -> g
      lstm_window_[index][a] =
        static_cast<float>(
          raw.accel[a] / MTS_STANDARD_GRAVITY_MS2
        );

      // Raw angular velocity rad/s
      lstm_window_[index][a + 3] =
        static_cast<float>(raw.gyro[a]);
    }

    if (lstm_samples_ < 30)
    {
      ++lstm_samples_;
    }
  }

  // ------------------------------------------------
  // Stage 3: Frame measurements
  // ------------------------------------------------

  if (d.active)
    out.flags |= FRAME_TREMOR_ACTIVE;

  if (dr.active)
    out.flags |= FRAME_ROTATION_ACTIVE;

  out.tremor_disp = so.tremor_disp;
  out.tremor_acc = so.tremor_acc;
  out.tremor_hz = d.hz;
  out.tremor_amp = d.amp;
  out.confidence = d.conf;

  out.tremor_rot = sr.tremor_disp;
  out.tremor_rot_rate = sr.tremor_acc;
  out.rot_hz = dr.hz;
  out.rot_amp = dr.amp;
  out.rot_confidence = dr.conf;

  out.orientation = fo.q;

  // Six processed predictor targets
  const double actual[AXES] = {
    so.tremor_disp.v[0],
    so.tremor_disp.v[1],
    so.tremor_disp.v[2],
    sr.tremor_disp.v[0],
    sr.tremor_disp.v[1],
    sr.tremor_disp.v[2]
  };

  // ------------------------------------------------
  // Stage 4: Prediction history
  // ------------------------------------------------

  const uint32_t ring = K_MAX + 1;

  const uint32_t now_slot =
    (uint32_t)(since_reset_ % ring);

  const bool old_enough =
    since_reset_ >= horizon_;

  const uint32_t then_slot =
    old_enough
      ? (uint32_t)((since_reset_ - horizon_) % ring)
      : 0;

  int64_t predictor_delay = 0;

  for (uint32_t i = 0; i < n_slots_; ++i)
  {
    Slot& slot = slots_[i];
    Frame_Prediction& p = out.predictions[i];

    p.predictor_id = slot.id;

    if (!slot.ops && slot.id != PREDICTOR_LSTM)
    {
      continue;
    }

    // ----------------------------------------------
    // Evaluate previous predictions
    // ----------------------------------------------

    if (old_enough && slot.made_valid[then_slot])
    {
      p.flags |= PRED_FORECAST_VALID;

      for (int a = 0; a < 3; ++a)
      {
        p.forecast.v[a] =
          slot.made[then_slot][a];

        p.residual.v[a] =
          actual[a] - p.forecast.v[a];

        p.forecast_rot.v[a] =
          slot.made[then_slot][a + 3];

        p.residual_rot.v[a] =
          actual[a + 3] - p.forecast_rot.v[a];
      }

      if (d.active)
        p.flags |= PRED_RESIDUAL_VALID;

      if (dr.active)
        p.flags |= PRED_RESIDUAL_ROT_VALID;
    }

    bool made = false;

    // ----------------------------------------------
    // LSTM prediction
    // ----------------------------------------------

    if (slot.id == PREDICTOR_LSTM)
    {
      if (lstm_ && lstm_samples_ == 30)
      {
        try
        {
          const auto forecast =
            lstm_->predict(lstm_window_);

          // Keep raw IMU forecasts for React
          p.imu_forecast_count = 5;

          for (int t = 0; t < 5; ++t)
          {
            for (int c = 0; c < 6; ++c)
            {
              p.imu_future[t][c] =
                forecast[t][c];
            }
          }

          // Convert into the same processed signal
          // representation as inversion and WFLC
          if (horizon_ <= 5)
          {
            made = project_lstm_via_filters(
              forecast,
              raw,
              cal_,
              fusion_,
              separation_,
              separation_rot_,
              fusion_predict_,
              separation_predict_,
              separation_rot_predict_,
              rate_hz_,
              horizon_,
              slot.made[now_slot]
            );
          }
        }
        catch (const std::exception& e)
        {
          MTS_LOG_ERROR(
            "LSTM inference failed: %s",
            e.what()
          );

          lstm_.reset();
          lstm_samples_ = 0;
        }
      }
    }

    // ----------------------------------------------
    // WFLC and inversion prediction
    // ----------------------------------------------

    else
    {
      for (int a = 0; a < AXES; ++a)
      {
        double ahead[K_MAX];

        slot.ops->update(
          slot.state[a],
          actual[a]
        );

        const size_t k = slot.ops->predict(
          slot.state[a],
          ahead,
          horizon_
        );

        const bool axis = k >= horizon_;

        slot.made[now_slot][a] =
          axis ? ahead[horizon_ - 1] : 0.0;

        made = made || axis;
      }
    }

    slot.made_valid[now_slot] = made;

    // ----------------------------------------------
    // Counter-signal generation
    // ----------------------------------------------

    if (slot.id == active_)
    {
      predictor_delay =
        slot.ops
          ? slot.ops->delay_ns(slot.state[0])
          : 0;

      if (p.flags & PRED_FORECAST_VALID)
      {
        for (int a = 0; a < 3; ++a)
        {
          out.counter.v[a] =
            -p.forecast.v[a];

          out.counter_rot.v[a] =
            -p.forecast_rot.v[a];
        }

        if (d.active)
          out.flags |= FRAME_COUNTER_VALID;

        if (dr.active)
          out.flags |= FRAME_COUNTER_ROT_VALID;
      }
    }
  }

  out.n_predictions = n_slots_;
  ++since_reset_;

  // Pipeline latency
  const int64_t stage1 =
    fo.delay_ns + so.delay_ns > sr.delay_ns
      ? fo.delay_ns + so.delay_ns
      : sr.delay_ns;

  out.pipeline_delay_ns =
    stage1 + predictor_delay + (now_ns - raw.t_ns);
}


// --------------------------------------------------
// Processing thread
// --------------------------------------------------

Processing::Processing(
    double rate_hz,
    Sample_Queue& samples,
    Mailbox<Pipeline_Control>& control,
    Queue<Server_Message>& frames,
    Mailbox<Led_Command>& led)
  : core_(rate_hz)
  , samples_(samples)
  , control_(control)
  , frames_(frames)
  , led_(led)
  , msg_{}
  , stopping_(false)
{
}


Processing::~Processing()
{
  stop();
}


bool Processing::start()
{
  if (!core_.valid())
  {
    MTS_LOG_ERROR(
      "processing: stage 1 could not be built"
    );
    return false;
  }

  if (thread_.joinable())
  {
    MTS_LOG_ERROR(
      "processing: already started"
    );
    return false;
  }

  thread_ = std::thread(
    &Processing::run,
    this
  );

  return true;
}


void Processing::stop()
{
  if (!thread_.joinable())
  {
    return;
  }

  stopping_.store(
    true,
    std::memory_order_relaxed
  );

  thread_.join();
}


void Processing::run()
{
  Led_Command off{};
  off.mode = LED_MODE_OFF;
  led_.write(off);

  Pipeline_Control ctl{};
  
  while (!stopping_.load(
      std::memory_order_relaxed))
  {
    Raw_Sample s{};

    if (!samples_.pop(s, PROCESSING_POLL_NS))
    {
      continue;
    }

    if (control_.read(ctl))
    {
      core_.apply(ctl);
    }

    msg_.kind = SERVER_MSG_FRAME;

    core_.process(
      s,
      mts_time_now_ns(),
      msg_.frame
    );

    if (ctl.running)
    {
      frames_.try_push(msg_);
    }
  }
}

} // namespace mts
