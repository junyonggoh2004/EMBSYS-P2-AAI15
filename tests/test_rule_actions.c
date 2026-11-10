/* D1 task 1.2: a rule's action pins are checked on this board only for its own
 * gpio:/pwm: actions; a cfg.to or cmd.to payload is for another node, which
 * checks its own pins. The real rules.c and rule_expr.c are compiled. */
#include <assert.h>
#include <stdio.h>
#include "hal/hal.h"
#include "rule_engine/rules.h"
#include "rule_engine/rule_action.h"
#include "inference_engine/inference_manager.h"

bool hal_gpio_valid(int pin) { return pin >= 0 && pin <= 48; } /* the S3's GPIO range */
uint64_t hal_time_us(void) { return 0; }
void fire_action(const rule_t *R, double value, uint32_t ts) { (void)R; (void)value; (void)ts; }
bool inference_mgr_is_ready(void) { return false; }
bool inference_mgr_run(void) { return false; }
bool inference_mgr_set_inputs(const float *inputs, int count) { (void)inputs; (void)count; return false; }
bool inference_mgr_get_output(int index, float *out) { (void)index; (void)out; return false; }

static bool add(const char *action)
{
    char line[512];
    rules_begin();
    rules_feed_line("name=R");
    rules_feed_line("source=S");
    rules_feed_line("calc=s=u8(0)");
    rules_feed_line("when=s==0");
    snprintf(line, sizeof line, "action=%s", action);
    rules_feed_line(line);
    bool ok = rules_end();
    rules_reset();
    return ok;
}

int main(void)
{
    /* This board's own pins are still checked when the rule is added (D-04). */
    assert(add("gpio:8=HIGH"));
    assert(!add("gpio:99=HIGH"));
    assert(!add("pwm:60=50%"));
    assert(add("batch: gpio:8=HIGH | pwm:9=50%"));
    assert(!add("batch: gpio:8=HIGH | gpio:77=LOW"));

    /* Payloads for another node are not checked against this board's pins. */
    assert(add("cfg.to:s3-002:{\"configs\":[{\"name\":\"LEDJ\",\"proto\":\"gpio\",\"gpio\":{\"pin\":8}}]}"));
    assert(add("cfg.to:s3-002:{configs:[{name:LEDJ,proto:gpio,mode:poll,freq_hz:1,gpio:{pin:8,mode:digital}}]}"));
    /* A forwarded rule's gpio: action names the other board's pin, not this one's. */
    assert(add("cfg.to:s3-002:BEGINRULE\\|name=X\\|source=Y\\|when=1\\|action=gpio:99=HIGH\\|ENDRULE"));
    assert(add("cfg.to:s3-002:BEGINCFG\\|name=LED\\|proto=gpio\\|gpio.pin=8\\|ENDCFG"));
    assert(add("cmd.to:s3-002:BEGINRULE gpio:99=HIGH"));
    /* A local action before a remote one is still checked. */
    assert(!add("batch: gpio:99=HIGH | cmd.to:s3-002:RUN"));
    assert(add("batch: gpio:8=HIGH | cmd.to:s3-002:RUN"));

    puts("Rule action pins: PASS (local gpio/pwm pins checked; cfg.to/cmd.to payloads left to the receiving node)");
    return 0;
}
