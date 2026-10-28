/* ===================================================================================================
 * core/include/core/detect/detect.h
 *
 * Tremor detector: is there tremor right now, at what frequency and
 * amplitude, how sure. Band-power ratio, peak narrowness and amplitude, with
 * hysteresis (separate on/off thresholds) so the flag does not flicker.
 * Predictors only act while it says active.
 *
 * Opaque, same contract as every stage 1 module (see fusion.h).
 *
 * v0: one WFLC per axis on tremor_disp, and the running power of each
 * axis over window_s. The axis with the most power is the tremor's:
 *  - hz   = its WFLC's frequency
 *  - amp  = sqrt(2 * power): the peak of a sine of that power
 *  - conf = 1 - its WFLC's error_ratio: how much of the motion one
 *    oscillation explains (a narrow peak scores high, broadband motion low)
 *  - active turns on at conf >= on_threshold with amp >= min_amp, and off
 *    at conf < off_threshold or amp < min_amp / 2
 * Units: those of tremor_disp (m for translation, rad for rotation); amp
 * comes out in them too.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_DETECT_DETECT_H
#define MTS_CORE_DETECT_DETECT_H

#include <core/separation/separation.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Detector Detector;

typedef struct Detect_Config
{
  double rate_hz; // measured sample rate
  double on_threshold; // score above which tremor turns on
  double off_threshold; // score below which it turns off (< on_threshold: hysteresis)
  double min_amp; // amplitude below which it never turns on, in tremor_disp's units (m, or rad)
  double window_s; // analysis window length
} Detect_Config;

typedef struct Detect_Output
{
  bool active;
  double hz; // dominant frequency
  double amp; // amplitude (peak), in tremor_disp's units
  double conf; // 0..1
} Detect_Output;

// on 0.6, off 0.4, 0.05 mm (min_amp for a displacement in m), 1 s window
Detect_Config detect_config_default(double rate_hz);

// NULL (and a logged error) on a bad config or out of memory
Detector* detect_create(const Detect_Config* cfg);

// NULL is fine
void detect_destroy(Detector* d);

void detect_reset(Detector* d);

void detect_update(Detector* d, const Separation_Output* in, Detect_Output* out);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_DETECT_DETECT_H */
