/* ===================================================================================================
 * app/include/app/link/pipeline_control.h
 *
 * Pipeline_Control: what the orchestrator tells Processing, through a
 * latest-wins mailbox. Processing reads it once per sample, so a change
 * applies at a sample boundary (ui_contract.md, setup), and a burst of
 * changes (a slider dragged) costs nothing: only the last one is seen.
 *
 * cfg_gen changes with every new value: Processing compares it to decide
 * whether predictors must be reset.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_LINK_PIPELINE_CONTROL_H
#define MTS_APP_LINK_PIPELINE_CONTROL_H

#include <app/link/command.h>

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pipeline_Control
{
  uint32_t running; // 1: produce frames (state running). 0: samples are read and dropped
  uint32_t cfg_gen; // +1 on every change
  Runtime_Settings settings;
} Pipeline_Control;

static_assert(sizeof(Pipeline_Control) % 8 == 0, "Pipeline_Control must be a multiple of 8 bytes (mailbox)");

#ifdef __cplusplus
}
#endif

#endif /* MTS_APP_LINK_PIPELINE_CONTROL_H */
