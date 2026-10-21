/* ===================================================================================================
 * core/include/core/detect/detect.h
 *
 * SEAM HEADER: interface only, implementation to come.
 *
 * Tremor detector: is there tremor right now, at what frequency and
 * amplitude, how sure. Band-power ratio, peak narrowness and amplitude, with
 * hysteresis (separate on/off thresholds) so the flag does not flicker.
 * Predictors only act while it says active.
 *
 * Opaque, same contract as every stage 1 module (see fusion.h).
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
  double min_amp_m; // displacement amplitude below which it never turns on
  double window_s; // analysis window length
} Detect_Config;

typedef struct Detect_Output
{
  bool active;
  double hz; // dominant frequency
  double amp; // m, displacement amplitude
  double conf; // 0..1
} Detect_Output;

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

