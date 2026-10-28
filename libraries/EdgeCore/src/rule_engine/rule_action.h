// rule_engine/rule_action.h
#pragma once

#include <stdint.h>
#include "rule_engine/rules.h"

#ifdef __cplusplus
extern "C" {
#endif

// C-facing action dispatcher. event:<class> reports a rule decision only;
// it cannot establish observation-window completion/timing quality.
void fire_action(const rule_t *R, double value, uint32_t ts_ms);

#ifdef __cplusplus
}
#endif
