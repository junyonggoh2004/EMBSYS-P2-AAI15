#!/usr/bin/env python3
"""Send configpico2wedge REPL commands over USB serial and check each reply.

The same steps run against the Pico 2 W reference and the ESP32-S3 port, so
both boards are judged on identical commands.
"""

import argparse
import glob
import re
import sys
import time
from pathlib import Path

import serial

BAUD = 115200
STEP_TIMEOUT = 10.0
HEARTBEAT = re.compile(r"^\[SCHED\s+\d+ms\] run_all:|^\[MQTT\] Status:")

BOOT_CHECKS = [
    r"WiFi connect(ed| failed)",
    r"^\[INFER\] Inference manager initialised",
    r"^\[INFER\] Base model loaded: 16 inputs",
]

# (id, description, command, seconds to wait, patterns that must all appear)
STAGES = {
    "4a": [
        ("repl-help", "HELP lists the commands", "HELP", 2.5, [r"^Commands:"]),
        ("repl-show", "SHOW with nothing configured", "SHOW", 2.5,
         [r"^\[SHOW\] sampling=0", r"^\[RULE\] count=0"]),
        ("repl-run", "RUN starts sampling", "RUN", 2.5, [r"^RUN$"]),
        ("repl-stop", "STOP stops sampling", "STOP", 2.5, [r"^STOP$"]),
        ("repl-unknown", "An unknown command is rejected", "FOO", 2.5, [r"^Unknown: FOO"]),
        ("cfg-digital", "GPIO digital config is accepted",
         "BEGINCFG|name=DIG|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
         "|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: DIG\)"]),
        ("repl-show-cfg", "SHOW lists the configured sensor", "SHOW", 2.5, [r"name=DIG"]),
        ("rule-log", "A rule with a log action is accepted",
         "BEGINRULE|name=R_LOG|source=DIG|calc=s=u8(0)|when=s==0|action=log:pin low|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("rule-infer", "A rule with batched inference actions is accepted",
         r"BEGINRULE|name=R_INF|source=DIG|calc=s=u8(0)|when=1"
         r"|action=batch: infer.set:0:1.0 \| infer.run|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("run-data", "RUN produces sensor data, fires the log rule and runs inference", "RUN", 5.0,
         [r"^proto=gpio src=DIG len=3", r"^\[RULE\] R_LOG: pin low", r"^\[INFER\] invoke=\d+us"]),
        ("run-stop", "STOP after sampling", "STOP", 2.5, [r"^STOP$"]),
        ("repl-showrules", "SHOWRULES reports both rules", "SHOWRULES", 2.5, [r"^\[RULE\] count=2"]),
        ("repl-clearrules", "CLEARRULES removes the rules", "CLEARRULES", 2.5, [r"^\[RULE\] reset"]),
        ("repl-clear", "CLEAR removes sensors and rules", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # Needs one jumper from GP21 (driven by rule actions) to GP22 (read as an input).
    "4b": [
        ("cfg-input", "GPIO digital input on GP22 is accepted",
         "BEGINCFG|name=IN|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
         "|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: IN\)"]),
        ("rule-high", "Rule to drive GP21 HIGH is accepted",
         "BEGINRULE|name=R_HIGH|source=IN|calc=s=u8(0)|when=s==0|action=gpio:21=HIGH|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("gpio-high", "HIGH action drives GP21 and GP22 reads 1", "RUN", 5.0,
         [r"^\[RULE\] R_HIGH: gpio 21=HIGH", r"src=IN len=3 ts=\d+ : 01 "]),
        ("stop-high", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("rule-low", "Rule to drive GP21 LOW is accepted",
         "BEGINRULE|name=R_LOW|source=IN|calc=s=u8(0)|when=s==1|action=gpio:21=LOW|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("gpio-low", "LOW action drives GP21 and GP22 reads 0", "RUN", 5.0,
         [r"^\[RULE\] R_LOW: gpio 21=LOW", r"src=IN len=3 ts=\d+ : 00 "]),
        ("stop-low", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-rules-1", "CLEARRULES", "CLEARRULES", 2.5, [r"^\[RULE\] reset"]),
        ("rule-toggle", "Rule to toggle GP21 is accepted",
         "BEGINRULE|name=R_TGL|source=IN|calc=s=u8(0)|when=1|action=gpio:21=TOGGLE|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("gpio-toggle", "TOGGLE action flips GP21 and GP22 reads both states", "RUN", 6.0,
         [r"^\[RULE\] R_TGL: gpio 21=TOGGLE", r"src=IN len=3 ts=\d+ : 00 ", r"src=IN len=3 ts=\d+ : 01 "]),
        ("stop-toggle", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-rules-2", "CLEARRULES", "CLEARRULES", 2.5, [r"^\[RULE\] reset"]),
        ("rule-pulse", "Rule to pulse GP21 is accepted",
         "BEGINRULE|name=R_PLS|source=IN|calc=s=u8(0)|when=1|action=gpio:21=PULSE:200|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("gpio-pulse", "PULSE action runs", "RUN", 4.0, [r"^\[RULE\] R_PLS: gpio 21=PULSE 200ms"]),
        ("stop-pulse", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-1", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-counter", "GPIO counter on GP22 is accepted",
         "BEGINCFG|name=CNT|proto=gpio|gpio.mode=counter|gpio.pin=22|gpio.pull=down"
         "|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: CNT\)"]),
        ("rule-count", "Rule to toggle GP21 on every sample is accepted",
         "BEGINRULE|name=R_CNT|source=CNT|calc=c=u8(0)|when=1|action=gpio:21=TOGGLE|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("gpio-counter", "Counter counts the edges GP21 makes", "RUN", 7.0,
         [r"src=CNT len=4 ts=\d+ : 0[3-9A-F] 00 00 00"]),
        ("stop-counter", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-rules-3", "CLEARRULES", "CLEARRULES", 2.5, [r"^\[RULE\] reset"]),
        ("rule-pwm", "Rule with a PWM action is accepted",
         "BEGINRULE|name=R_PWM|source=CNT|calc=c=u8(0)|when=1|action=pwm:21=0.5|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("pwm-run", "PWM action sets 50% duty on GP21", "RUN", 4.0,
         [r"^\[RULE\] R_PWM: pwm 21 duty=0\.500"]),
        ("stop-pwm", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-2", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # Needs one jumper from GP16 (UART0 TX) to GP17 (UART0 RX).
    "4c-uart": [
        ("cfg-uart-a", "UART config on GP16/GP17 is accepted",
         "BEGINCFG|name=ULOOP|proto=uart|uart.tx=16|uart.rx=17|uart.baud=115200"
         "|uart.pre=0xA5 0x5A|uart.post_delay_ms=5|uart.read_len=2|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"UART\(tx=16,rx=17,baud=115200\)", r"^CFG: ok \(added #\d+: ULOOP\)"]),
        # A stray 0x00 when the UART pins are first set up can offset the framing by one byte.
        ("uart-echo-a", "Bytes sent on TX come back on RX in order", "RUN", 4.0,
         [r"^proto=uart src=ULOOP len=2 ts=\d+ : (A5 5A|5A A5)$"]),
        ("stop-a", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-a", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-uart-b", "A second UART config with different bytes is accepted",
         "BEGINCFG|name=ULOOP2|proto=uart|uart.tx=16|uart.rx=17|uart.baud=115200"
         "|uart.pre=0x12 0x34 0x56|uart.post_delay_ms=5|uart.read_len=3|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: ULOOP2\)"]),
        ("uart-echo-b", "The new bytes come back, so the data follows what is sent", "RUN", 4.0,
         [r"^proto=uart src=ULOOP2 len=3 ts=\d+ : 12 34 56$"]),
        ("stop-b", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-b", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # HC-SR04 with Trig on GP16 and Echo on GP17; move a hand in front of it during the RUN step.
    "4c-sonic": [
        ("cfg-sonic", "HC-SR04 pulse config is accepted",
         "BEGINCFG|name=HCSR04|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=2|gpio.trig=16"
         "|gpio.echo=17|gpio.trig_us=10|gpio.pulse_timeout_us=25000|ENDCFG",
         2.5, [r"method=pulse", r"^CFG: ok \(added #\d+: HCSR04\)"]),
        # Width must be under the 25 000 us timeout (0x61A8), so a reading only counts
        # when an object is actually in range; a clipped A9 61 means nothing was seen.
        ("sonic-read", "A real distance is measured while an object is held in front of the sensor", "RUN", 45.0,
         [r"^proto=gpio src=HCSR04 len=3 ts=\d+ : [0-9A-F]{2} ([0-5][0-9A-F]|60) 01$"]),
        ("stop-sonic", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-sonic", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # Grove Digital Light Sensor v1.1 (TSL2561, address 0x29): SDA on GP16, SCL on GP17.
    # The pre bytes are register/value pairs: power on, then 402 ms integration at 1x gain.
    "4c-i2c": [
        ("cfg-tsl-id", "I2C config reading the TSL2561 ID register is accepted",
         "BEGINCFG|name=TSLID|proto=i2c|mode=poll|freq_hz=1|i2c.sda=16|i2c.scl=17|i2c.addr=0x29"
         "|i2c.pre=0x80 0x03 0x81 0x02|i2c.reg=0x8A|i2c.reg_size=1|i2c.read_len=1|i2c.restart=1|ENDCFG",
         2.5, [r"I2C\(sda=16,scl=17,addr=0x29", r"^CFG: ok \(added #\d+: TSLID\)"]),
        ("i2c-id", "The chip answers with a TSL2561 part number", "RUN", 4.0,
         [r"^proto=i2c src=TSLID len=1 ts=\d+ : [15][0-9A-F]$"]),
        ("stop-id", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-id", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-tsl-data", "I2C config reading both light channels is accepted",
         "BEGINCFG|name=LUX|proto=i2c|mode=poll|freq_hz=1|i2c.sda=16|i2c.scl=17|i2c.addr=0x29"
         "|i2c.pre=0x80 0x03 0x81 0x02|i2c.reg=0x8C|i2c.reg_size=1|i2c.read_len=4|i2c.restart=1|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: LUX\)"]),
        ("i2c-light", "Light readings arrive while the sensor is covered and uncovered", "RUN", 30.0,
         [r"^proto=i2c src=LUX len=4 ts=\d+ : ([0-9A-F]{2} ){3}[0-9A-F]{2}$"]),
        ("stop-light", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-light", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # Rules on real HC-SR04 data (Trig GP16, Echo GP17). Move an object in and out of 30 cm.
    # 1740 us of echo is about 30 cm.
    "sonic-rule": [
        ("clear-sr", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-sr", "HC-SR04 pulse config is accepted",
         "BEGINCFG|name=HCSR04|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=2|gpio.trig=16"
         "|gpio.echo=17|gpio.trig_us=10|gpio.pulse_timeout_us=25000|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: HCSR04\)"]),
        ("rule-near", "Rule for an object nearer than 30 cm is accepted",
         "BEGINRULE|name=R_NEAR|source=HCSR04|calc=w=u16le(0)|when=w<1740|action=log:near|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("rule-far", "Rule for an object 30 cm or further is accepted",
         "BEGINRULE|name=R_FAR|source=HCSR04|calc=w=u16le(0)|when=w>=1740|action=log:far|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("sonic-rules-run", "Both rules fire on real distance data as the object moves", "RUN", 40.0,
         [r"^\[RULE\] R_NEAR: near", r"^\[RULE\] R_FAR: far"]),
        ("stop-sr", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-sr-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # IR reflective module: VCC 3V3, GND, OUT on GP16. Three sensors read the same pin at once:
    # plain digital, inverted digital (T-02) and counter (T-05). Wave a hand over it slowly.
    "4d-ir": [
        ("clear-ir", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-ir", "IR sensor as a digital input is accepted",
         "BEGINCFG|name=IR|proto=gpio|gpio.mode=digital|gpio.pin=16|gpio.pull=up|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: IR\)"]),
        ("cfg-irinv", "The same pin with gpio.invert=1 is accepted",
         "BEGINCFG|name=IRINV|proto=gpio|gpio.mode=digital|gpio.pin=16|gpio.pull=up|gpio.invert=1"
         "|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: IRINV\)"]),
        ("cfg-ircnt", "The same pin as a counter is accepted",
         "BEGINCFG|name=IRCNT|proto=gpio|gpio.mode=counter|gpio.pin=16|gpio.pull=up|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: IRCNT\)"]),
        ("ir-run", "A hand over the sensor changes both states, inverted reads the opposite, counter counts",
         "RUN", 40.0,
         [r"src=IR len=3 ts=\d+ : 00 ", r"src=IR len=3 ts=\d+ : 01 ",
          r"src=IRINV len=3 ts=\d+ : 00 ", r"src=IRINV len=3 ts=\d+ : 01 ",
          r"src=IRCNT len=4 ts=\d+ : 0[2-9A-F] 00 00 00"]),
        ("stop-ir", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-ir-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # IR module retest on a fresh pin (OUT on GP20) with pull-down, so the result is decisive:
    # steady 00 = wire not connected, steady 01 = output never changes, both = sensor works.
    "4d-ir2": [
        ("clear-ir2", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-ir2", "IR sensor on GP20 with pull-down is accepted",
         "BEGINCFG|name=IR2|proto=gpio|gpio.mode=digital|gpio.pin=20|gpio.pull=down|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: IR2\)"]),
        ("cfg-ir2cnt", "The same pin as a counter is accepted",
         "BEGINCFG|name=IR2CNT|proto=gpio|gpio.mode=counter|gpio.pin=20|gpio.pull=down|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: IR2CNT\)"]),
        ("ir2-run", "Triggering the sensor flips GP20 both ways and the counter counts", "RUN", 40.0,
         [r"src=IR2 len=3 ts=\d+ : 00 ", r"src=IR2 len=3 ts=\d+ : 01 ",
          r"src=IR2CNT len=4 ts=\d+ : 0[2-9A-F] 00 00 00"]),
        ("stop-ir2", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-ir2-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # LM393 comparator board: VCC 3V3, GND, D0 on GP18. Turning its knob moves the threshold,
    # so D0 flips. Pull-down makes a disconnected wire read a steady 00 instead of a false 01.
    "4e-comp": [
        ("clear-comp", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-comp", "Comparator D0 as a digital input on GP18 is accepted",
         "BEGINCFG|name=COMP|proto=gpio|gpio.mode=digital|gpio.pin=18|gpio.pull=down|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: COMP\)"]),
        ("cfg-compcnt", "The same pin as a counter is accepted",
         "BEGINCFG|name=COMPCNT|proto=gpio|gpio.mode=counter|gpio.pin=18|gpio.pull=down|mode=poll|freq_hz=2|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: COMPCNT\)"]),
        ("comp-run", "Turning the knob flips D0 both ways and the counter counts the flips", "RUN", 40.0,
         [r"src=COMP len=3 ts=\d+ : 00 ", r"src=COMP len=3 ts=\d+ : 01 ",
          r"src=COMPCNT len=4 ts=\d+ : 0[2-9A-F] 00 00 00"]),
        ("stop-comp", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-comp-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # T-04: eight sensors at a high requested rate. Needs nothing wired to GP22.
    "t04": [
        ("clear-t04", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        *[(f"cfg-t04-{n}", f"Fast sensor {n} of 8 is accepted",
           f"BEGINCFG|name=F{n}|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
           "|mode=poll|freq_hz=20|ENDCFG",
           2.5, [rf"^CFG: ok \(added #\d+: F{n}\)"]) for n in range(1, 9)],
        ("run-t04", "All eight sensors keep publishing under load", "RUN", 12.0,
         [rf"^proto=gpio src=F{n} len=3" for n in range(1, 9)]),
        ("stop-t04", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("alive-t04", "The board still answers after the overload run", "SHOW", 2.5,
         [r"^\[SHOW\] sampling=0"]),
        ("clear-t04-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # T-03: an out-of-range GPIO number. Reference defect: it is accepted and returns fake readings.
    "t03": [
        ("clear-t03", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-t03", "DEFECT: a config with gpio.pin=99 is accepted",
         "BEGINCFG|name=BADPIN|proto=gpio|gpio.mode=digital|gpio.pin=99|mode=poll|freq_hz=1|ENDCFG",
         3.0, [r"GPIO\(pin=99\)", r"^CFG: ok \(added #\d+: BADPIN\)"]),
        ("run-t03", "DEFECT: the nonexistent pin publishes normal-looking readings", "RUN", 4.0,
         [r"^proto=gpio src=BADPIN len=3 ts=\d+ : 00 "]),
        ("stop-t03", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("alive-t03", "The board did not crash", "SHOW", 3.0, [r"^\[SHOW\] sampling=0"]),
        ("clear-t03-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # T-10: a false when= runs no action, while a true one on the same sensor still fires.
    "t10": [
        ("clear-t10", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("cfg-t10", "GPIO digital input on GP22 (nothing attached, reads low)",
         "BEGINCFG|name=DIG|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
         "|mode=poll|freq_hz=1|ENDCFG",
         2.5, [r"^CFG: ok \(added #\d+: DIG\)"]),
        ("rule-false", "Rule whose condition is false is accepted",
         "BEGINRULE|name=RF|source=DIG|calc=s=u8(0)|when=s==1|action=log:should not fire|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("rule-true", "Control rule whose condition is true is accepted",
         "BEGINRULE|name=RT|source=DIG|calc=s=u8(0)|when=s==0|action=log:control fired|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("run-t10", "The true rule fires and the false rule never does", "RUN", 6.0,
         [r"^\[RULE\] RT: control fired", r"!^\[RULE\] RF:"]),
        ("stop-t10", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clear-t10-end", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
    ],
    # Invalid input and limits. Needs nothing wired to the board.
    "5-neg": [
        ("clear-0", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        # Reference defect: an unknown proto keeps the default (i2c) instead of being rejected.
        ("neg-proto", "DEFECT: an unsupported protocol is silently accepted as I2C on default pins",
         "BEGINCFG|name=BADP|proto=spi|mode=poll|freq_hz=1|ENDCFG",
         2.5, [r"BADP on i2c .*I2C\(sda=4,scl=5,addr=0x76", r"^CFG: ok \(added #\d+: BADP\)"]),
        ("neg-i2c-nodev", "An I2C device that does not answer is rejected",
         "BEGINCFG|name=NODEV|proto=i2c|mode=poll|freq_hz=1|i2c.sda=16|i2c.scl=17|i2c.addr=0x29"
         "|i2c.pre=0x80 0x03|i2c.reg=0x8A|i2c.reg_size=1|i2c.read_len=1|i2c.restart=1|ENDCFG",
         2.5, [r"init_cfg FAILED for NODEV", r"^CFG: add failed \(-2\)"]),
        ("neg-nopin", "DEFECT: a digital config without gpio.pin is silently accepted on GP2",
         "BEGINCFG|name=NOPIN|proto=gpio|gpio.mode=digital|mode=poll|freq_hz=1|ENDCFG",
         2.5, [r"NOPIN on gpio .*GPIO\(pin=2\)", r"^CFG: ok \(added #\d+: NOPIN\)"]),
        ("clear-1", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        *[(f"slot-{n}", f"Sensor slot {n} of 8 is accepted",
           f"BEGINCFG|name=S{n}|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
           "|mode=poll|freq_hz=1|ENDCFG",
           2.5, [rf"^CFG: ok \(added #\d+: S{n}\)"]) for n in range(1, 9)],
        ("neg-slots", "A ninth sensor is refused",
         "BEGINCFG|name=S9|proto=gpio|gpio.mode=digital|gpio.pin=22|gpio.pull=down"
         "|mode=poll|freq_hz=1|ENDCFG",
         2.5, [r"^\[SCHED\] no free slots"]),
        ("neg-rule-nosource", "A rule without source= is rejected",
         "BEGINRULE|name=RN|calc=x=u8(0)|when=1|action=log:x|ENDRULE",
         2.5, [r"^\[RULE\] missing source=", r"^RULE: parse error"]),
        *[(f"rule-{n}", f"Rule {n} of 4 is accepted",
           f"BEGINRULE|name=R{n}|source=S1|calc=x=u8(0)|when=1|action=log:ok{n}|ENDRULE",
           2.5, [r"^RULE: ok"]) for n in range(1, 4)],
        ("rule-4-badpin", "DEFECT: rule 4, with a non-numeric GPIO pin in its action, is accepted",
         "BEGINRULE|name=R4|source=S1|calc=x=u8(0)|when=1|action=gpio:X=HIGH|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("neg-rule-full", "A fifth rule is refused",
         "BEGINRULE|name=R5|source=S1|calc=x=u8(0)|when=1|action=log:no|ENDRULE",
         2.5, [r"^\[RULE\] table full", r"^RULE: parse error"]),
        ("neg-badpin-run", "The bad GPIO pin is reported when the rule fires", "RUN", 4.0,
         [r"^\[RULE\] R4: bad gpio pin in action='gpio:X=HIGH'", r"^\[RULE\] R1: ok1"]),
        ("stop-neg", "STOP", "STOP", 2.5, [r"^STOP$"]),
        ("clearrules-neg", "CLEARRULES", "CLEARRULES", 2.5, [r"^\[RULE\] reset"]),
        ("neg-badexpr", "DEFECT: a malformed when= expression is accepted without validation",
         "BEGINRULE|name=RX|source=S1|calc=x=u8(0)|when=x>>>|action=log:x|ENDRULE",
         2.5, [r"^RULE: ok"]),
        ("clear-2", "CLEAR", "CLEAR", 2.5, [r"^CLEARED"]),
        ("neg-toolong", "A line longer than the 512-byte buffer is refused", "A" * 600, 2.5,
         [r"^Line too long"]),
    ],
}


def find_port():
    ports = sorted(glob.glob("/dev/tty.usbmodem*") + glob.glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def wait_for_port(timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        port = find_port()
        if port:
            return port
        time.sleep(0.1)
    return None


def read_for(ser, seconds, log):
    lines, buf = [], b""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        buf += ser.read(512)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip("\r")
            log.write(f"{time.strftime('%H:%M:%S')} {line}\n")
            lines.append(line)
    log.flush()
    return lines


def all_seen(lines, patterns):
    # A pattern starting with "!" must NOT appear, so it can never end a step early.
    return all(any(re.search(p, line) for line in lines) for p in patterns if not p.startswith("!"))


def run_step(ser, wait, patterns, log):
    """Read for at least `wait` seconds, then keep reading until every pattern
    has appeared or STEP_TIMEOUT runs out. USB delivery can lag by seconds."""
    lines, start = [], time.monotonic()
    while True:
        lines += [line for line in read_for(ser, 0.5, log) if not HEARTBEAT.search(line)]
        elapsed = time.monotonic() - start
        if elapsed >= wait and all_seen(lines, patterns):
            return lines
        if elapsed >= max(wait, STEP_TIMEOUT):
            return lines


def check(step_id, description, lines, patterns):
    missing, evidence = [], []
    for pattern in patterns:
        if pattern.startswith("!"):
            hit = next((line for line in lines if re.search(pattern[1:], line)), None)
            if hit is not None:
                missing.append(f"{pattern} (appeared: {hit})")
            continue
        hit = next((line for line in lines if re.search(pattern, line)), None)
        if hit is None:
            missing.append(pattern)
        else:
            evidence.append(hit)
    return {"id": step_id, "description": description, "passed": not missing,
            "missing": missing, "evidence": evidence}


def capture_after_replug(seconds, log):
    print("Unplug the board, then plug it back in normally (no BOOTSEL).", flush=True)
    end = time.monotonic() + 60
    while find_port() and time.monotonic() < end:
        time.sleep(0.1)
    port = wait_for_port(600)
    if not port:
        sys.exit("board did not come back within 10 minutes")
    ser = serial.Serial(port, BAUD, timeout=0.1)
    log.write(f"--- boot capture on {port} ---\n")
    return ser, read_for(ser, seconds, log)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", default="4a", choices=sorted(STAGES))
    parser.add_argument("--port", help="serial port (default: first usbmodem/ACM port found)")
    parser.add_argument("--log", help="raw serial log path (default: parity/logs/<stage>.log)")
    parser.add_argument("--boot", action="store_true",
                        help="wait for the board to be replugged and check its boot log first")
    args = parser.parse_args()

    log_path = Path(args.log or f"parity/logs/{args.stage}.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    results = []

    with open(log_path, "w") as log:
        if args.boot:
            ser, boot_lines = capture_after_replug(15.0, log)
            results.append(check("boot", "Board boots, tries WiFi and loads the built-in model",
                                 boot_lines, BOOT_CHECKS))
        else:
            port = args.port or find_port()
            if not port:
                sys.exit("no serial port found; is the board plugged in and not held by another terminal?")
            ser = serial.Serial(port, BAUD, timeout=0.1)
            read_for(ser, 1.0, log)

        ser.write(b"\r\n")
        read_for(ser, 1.5, log)

        for step_id, description, command, wait, patterns in STAGES[args.stage]:
            log.write(f"{time.strftime('%H:%M:%S')} --- {step_id}: {command}\n")
            ser.write((command + "\r\n").encode())
            lines = run_step(ser, wait, patterns, log)
            results.append(check(step_id, description, lines, patterns))
        ser.close()

    for r in results:
        status = "PASS" if r["passed"] else "FAIL"
        print(f"{status}  {r['id']:<16} {r['description']}")
        for line in r["evidence"]:
            print(f"        seen: {line[:110]}")
        for pattern in r["missing"]:
            print(f"        MISSING: {pattern}")
    passed = sum(r["passed"] for r in results)
    print(f"\n{passed}/{len(results)} passed. Raw log: {log_path}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
