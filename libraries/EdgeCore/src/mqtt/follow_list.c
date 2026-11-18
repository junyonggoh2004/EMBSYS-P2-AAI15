// follow_list.c — see follow_list.h for task ownership.
#include "mqtt/follow_list.h"

#include <stdio.h>
#include <string.h>

static char s_topics[FOLLOW_LIST_MAX][FOLLOW_LIST_TOPIC_MAX];
static int s_count;          // published with release after the slot is written
static uint32_t s_generation; // incremented by every clear

static int find(const char *topic, int count)
{
    for (int i = 0; i < count; i++)
        if (strcmp(s_topics[i], topic) == 0) return i;
    return -1;
}

follow_add_t follow_list_check(const char *node, const char *source, char *topic_out)
{
    if (!node || !*node || !source || !*source || !topic_out) return FOLLOW_INVALID;
    int n = snprintf(topic_out, FOLLOW_LIST_TOPIC_MAX, "pico/%s/sensor/%s/line", node, source);
    if (n < 0 || n >= FOLLOW_LIST_TOPIC_MAX) {
        topic_out[0] = 0;
        return FOLLOW_INVALID;
    }
    int count = __atomic_load_n(&s_count, __ATOMIC_RELAXED);
    if (find(topic_out, count) >= 0) return FOLLOW_KNOWN;
    return count < FOLLOW_LIST_MAX ? FOLLOW_ADDED : FOLLOW_FULL;
}

follow_add_t follow_list_add(const char *topic)
{
    if (!topic || !*topic || strlen(topic) >= FOLLOW_LIST_TOPIC_MAX) return FOLLOW_INVALID;
    int count = __atomic_load_n(&s_count, __ATOMIC_RELAXED);
    if (find(topic, count) >= 0) return FOLLOW_KNOWN;
    if (count >= FOLLOW_LIST_MAX) return FOLLOW_FULL;
    strcpy(s_topics[count], topic);
    __atomic_store_n(&s_count, count + 1, __ATOMIC_RELEASE);
    return FOLLOW_ADDED;
}

int follow_list_count(void)
{
    return __atomic_load_n(&s_count, __ATOMIC_RELAXED);
}

const char *follow_list_topic(int index)
{
    return (index >= 0 && index < follow_list_count()) ? s_topics[index] : NULL;
}

void follow_list_clear(void)
{
    // Generation first, then a release fence, so no slot reuse after this
    // clear can become visible to a reader that still sees the old generation.
    __atomic_add_fetch(&s_generation, 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s_count, 0, __ATOMIC_RELEASE);
}

uint32_t follow_list_snapshot(int *count)
{
    uint32_t generation = __atomic_load_n(&s_generation, __ATOMIC_ACQUIRE);
    if (count) *count = __atomic_load_n(&s_count, __ATOMIC_ACQUIRE);
    return generation;
}

bool follow_list_copy(int index, char *out, size_t out_size)
{
    if (!out || out_size == 0 || index < 0 || index >= FOLLOW_LIST_MAX) return false;
    size_t i = 0;
    for (; i + 1 < out_size && i < FOLLOW_LIST_TOPIC_MAX - 1; i++) {
        char c = __atomic_load_n(&s_topics[index][i], __ATOMIC_RELAXED);
        out[i] = c;
        if (!c) break;
    }
    out[i < out_size ? i : out_size - 1] = 0;
    return true;
}

bool follow_list_unchanged(uint32_t generation)
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return __atomic_load_n(&s_generation, __ATOMIC_RELAXED) == generation;
}
