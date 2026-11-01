#include "application/event_identity.h"
static uint64_t s_node;
static uint32_t s_boot, s_sequence;
bool event_identity_install(uint64_t node, uint32_t boot)
{
    if (!node || !boot || s_node) return false;
    s_node = node; s_boot = boot;
    return true;
}
bool event_identity_ready(void) { return s_node && s_boot && s_sequence != UINT32_MAX; }
bool event_identity_next(uint64_t *node, uint32_t *boot, uint32_t *sequence)
{
    if (!node || !boot || !sequence || !event_identity_ready()) return false;
    *node = s_node; *boot = s_boot; *sequence = ++s_sequence;
    return true;
}
