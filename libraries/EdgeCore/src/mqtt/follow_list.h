// follow_list.h
// Remote-sensor topics this node follows, re-subscribed after every broker
// reconnect. Portable: no RTOS, SDK or MQTT client calls.
//
// Ownership: exactly one writer task (the task that applies configuration and
// CLEAR) calls follow_list_check(), follow_list_add() and follow_list_clear().
// Exactly one reader task (the one that owns the MQTT client) uses
// follow_list_snapshot(), follow_list_copy() and follow_list_unchanged() to
// resubscribe. Appends only write slots beyond any count the reader has
// loaded; a clear changes the generation before slots can be reused, so the
// reader discards a copy taken across a clear and starts again.
#ifndef FOLLOW_LIST_H
#define FOLLOW_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOLLOW_LIST_MAX        8
#define FOLLOW_LIST_TOPIC_MAX  160

typedef enum {
    FOLLOW_ADDED = 0,    // new topic (add: recorded; check: would be recorded)
    FOLLOW_KNOWN,        // already recorded; caller may subscribe again
    FOLLOW_FULL,         // not recorded: it would be lost on reconnect
    FOLLOW_INVALID,      // empty identity or topic longer than the slot
} follow_add_t;

// Writer side. follow_list_check() changes nothing: it builds
// pico/<node>/sensor/<source>/line into topic_out (FOLLOW_LIST_TOPIC_MAX bytes)
// and reports what follow_list_add() of that topic would return, so callers
// can refuse a full list before registering any other state.
follow_add_t follow_list_check(const char *node, const char *source, char *topic_out);
follow_add_t follow_list_add(const char *topic);
int          follow_list_count(void);
const char  *follow_list_topic(int index);     // writer side only; NULL if out of range
void         follow_list_clear(void);

// Reader side: copy entries one at a time and use a copy only while the
// generation taken by follow_list_snapshot() is unchanged.
uint32_t follow_list_snapshot(int *count);
bool     follow_list_copy(int index, char *out, size_t out_size);
bool     follow_list_unchanged(uint32_t generation);

#ifdef __cplusplus
}
#endif

#endif
