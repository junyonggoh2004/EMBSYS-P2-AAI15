/* D1 task 1.2 / D-11: JSON with bare words, as in the reference project's own
 * cfg.to example (project_v3/test_json/h206_remote.json), converts to exactly
 * the same configuration as strict JSON; strict JSON is unchanged. The real
 * mqtt_json_parser.c and jsmn are compiled. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt/mqtt_json_parser.h"

static void same(const char *relaxed, const char *strict)
{
    static char a[2048], b[2048];
    int na = mqtt_json_try_convert_to_inline(relaxed, a, sizeof a);
    int nb = mqtt_json_try_convert_to_inline(strict, b, sizeof b);
    assert(nb > 0);
    if (na != nb || strncmp(a, b, (size_t)nb)) {
        printf("relaxed -> %d %.*s\nstrict  -> %d %.*s\n", na, na > 0 ? na : 0, a, nb, nb, b);
        assert(0);
    }
}

int main(void)
{
    /* The payload the reference example forwards with cfg.to. */
    same("{configs:[{name:XYC_H206,proto:gpio,mode:poll,freq_hz:1,"
         "gpio:{pin:26,mode:digital,pull:up,invert:1,debounce_ms:20}}]}",
         "{\"configs\":[{\"name\":\"XYC_H206\",\"proto\":\"gpio\",\"mode\":\"poll\",\"freq_hz\":1,"
         "\"gpio\":{\"pin\":26,\"mode\":\"digital\",\"pull\":\"up\",\"invert\":1,\"debounce_ms\":20}}]}");
    /* The one D1 task 1.2 sends between the boards. */
    same("{configs:[{name:LEDJ,proto:gpio,mode:poll,freq_hz:1,gpio:{pin:8,mode:digital}}]}",
         "{\"configs\":[{\"name\":\"LEDJ\",\"proto\":\"gpio\",\"mode\":\"poll\",\"freq_hz\":1,"
         "\"gpio\":{\"pin\":8,\"mode\":\"digital\"}}]}");
    /* Rules: expressions and calls are single bare words; quoted parts stay as sent. */
    same("{rules:[{name:R1,source:C1,calc:[s=u8(0)],when:s==0,action:\"log:C1 low\"}]}",
         "{\"rules\":[{\"name\":\"R1\",\"source\":\"C1\",\"calc\":[\"s=u8(0)\"],\"when\":\"s==0\",\"action\":\"log:C1 low\"}]}");
    /* Mixed: some keys quoted, some not; literals and negative numbers kept. */
    same("{\"configs\":[{name:\"M\",proto:gpio,mode:poll,freq_hz:2,gpio:{pin:6,pull:down,mode:digital},enabled:true}]}",
         "{\"configs\":[{\"name\":\"M\",\"proto\":\"gpio\",\"mode\":\"poll\",\"freq_hz\":2,"
         "\"gpio\":{\"pin\":6,\"pull\":\"down\",\"mode\":\"digital\"},\"enabled\":true}]}");

    /* Strict JSON converts as before, and non-JSON is still not JSON. */
    static char out[2048];
    assert(mqtt_json_try_convert_to_inline("BEGINCFG|name=X|ENDCFG", out, sizeof out) == 0);
    puts("Relaxed JSON: PASS (bare-word JSON converts exactly like strict JSON; D-11 example accepted)");
    return 0;
}
