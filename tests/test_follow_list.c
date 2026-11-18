/* Followed remote topics: capacity, CLEAR and reconnect resubscription. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt/follow_list.h"

/* What MQTTTask resubscribes after a reconnect. */
static int reconnect_topics(char out[][FOLLOW_LIST_TOPIC_MAX])
{
    int n;
    uint32_t gen = follow_list_snapshot(&n);
    for (int i = 0; i < n; i++) {
        assert(follow_list_copy(i, out[i], FOLLOW_LIST_TOPIC_MAX));
        assert(follow_list_unchanged(gen));
    }
    return n;
}

static follow_add_t follow(const char *node, const char *source, char *topic)
{
    follow_add_t state = follow_list_check(node, source, topic);
    return state == FOLLOW_ADDED ? follow_list_add(topic) : state;
}

int main(void)
{
    char topic[FOLLOW_LIST_TOPIC_MAX], seen[FOLLOW_LIST_MAX][FOLLOW_LIST_TOPIC_MAX];
    char source[16];

    assert(follow("nodeA", "AHT", topic) == FOLLOW_ADDED);
    assert(!strcmp(topic, "pico/nodeA/sensor/AHT/line"));
    assert(follow("nodeA", "AHT", topic) == FOLLOW_KNOWN);
    assert(follow_list_count() == 1);

    for (int i = 1; i < FOLLOW_LIST_MAX; i++) {
        snprintf(source, sizeof source, "S%d", i);
        assert(follow("nodeA", source, topic) == FOLLOW_ADDED);
    }
    /* Old behaviour: the ninth topic was subscribed once, never recorded, and
     * reported as success, so a reconnect silently lost it. Now it is refused
     * and the check leaves the list untouched. */
    assert(follow_list_check("nodeX", "AHT", topic) == FOLLOW_FULL);
    assert(follow_list_add(topic) == FOLLOW_FULL);
    assert(follow_list_count() == FOLLOW_LIST_MAX);
    assert(follow("nodeA", "AHT", topic) == FOLLOW_KNOWN);   /* re-follow still allowed */

    /* CLEAR forgets everything; a reconnect must not resubscribe old topics. */
    int n;
    uint32_t before = follow_list_snapshot(&n);
    assert(n == FOLLOW_LIST_MAX);
    follow_list_clear();
    assert(!follow_list_unchanged(before));      /* a reader copying across CLEAR retries */
    assert(follow_list_count() == 0 && follow_list_topic(0) == NULL);
    assert(reconnect_topics(seen) == 0);

    /* New node names after CLEAR are recorded and survive the reconnect. */
    assert(follow("nodeB", "IN_AHT", topic) == FOLLOW_ADDED);
    assert(follow("nodeB", "IN_LIGHT", topic) == FOLLOW_ADDED);
    assert(reconnect_topics(seen) == 2);
    assert(!strcmp(seen[0], "pico/nodeB/sensor/IN_AHT/line"));
    assert(!strcmp(seen[1], "pico/nodeB/sensor/IN_LIGHT/line"));

    /* Empty or overlong identities never reach the list. */
    char long_node[FOLLOW_LIST_TOPIC_MAX];
    memset(long_node, 'n', sizeof long_node - 1);
    long_node[sizeof long_node - 1] = 0;
    assert(follow_list_check("", "AHT", topic) == FOLLOW_INVALID);
    assert(follow_list_check("nodeB", "", topic) == FOLLOW_INVALID);
    assert(follow_list_check(long_node, "AHT", topic) == FOLLOW_INVALID && !topic[0]);
    assert(follow_list_add("") == FOLLOW_INVALID);
    assert(follow_list_count() == 2);

    /* A short reader buffer is still terminated. */
    char small[8];
    assert(follow_list_copy(0, small, sizeof small) && strlen(small) == sizeof small - 1);

    puts("Follow list capacity/CLEAR/reconnect regression: PASS");
    return 0;
}
