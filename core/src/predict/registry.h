/* ===================================================================================================
 * core/src/predict/registry.h
 *
 * Private to core/predict: the descriptors the registry lists. Each
 * predictor's .c defines one.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_PREDICT_REGISTRY_H
#define MTS_CORE_PREDICT_REGISTRY_H

#include <core/predict/predict.h>

extern const Predictor_Ops PREDICTOR_OPS_INVERSION;
extern const Predictor_Ops PREDICTOR_OPS_WFLC;

#endif /* MTS_CORE_PREDICT_REGISTRY_H */
