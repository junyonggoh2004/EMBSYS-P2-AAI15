#include <stdlib.h>   // strtoul
#include <string.h>   // strlen, strcmp, strchr, strtok, memcpy, strncpy
#include <ctype.h>    // tolower, toupper
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "bus/bus_common.h"

/* internal helpers */
static uint32_t parse_u32(const char *s) {
    if (!s) return 0;
    if (strlen(s) > 2 && s[0]=='0' && (s[1]=='x' || s[1]=='X')) return (uint32_t)strtoul(s, NULL, 16);
    return (uint32_t)strtoul(s, NULL, 10);
}

static float parse_f32(const char *s) {
    if (!s) return 0.0f;
    return strtof(s, NULL);   // handles "1", "0.5", "2.0", etc.
}


static void str_lower(char *s) { for (; *s; ++s) *s = (char)tolower(*s); }

/* state: 0=idle, 1=inside block */
static int s_in_block = 0;

void cfg_parse_begin(void) { s_in_block = 1;  }

void cfg_parse_line(const char *line_in, app_cfg_t *cfg) {
    if (!s_in_block) return ;

    /* copy & trim */
    char line[160];
    strncpy(line, line_in, sizeof(line)-1);
    line[sizeof(line)-1] = 0;

    /* strip CR/LF */
    size_t n = strlen(line);
    while (n && (line[n-1]=='\r' || line[n-1]=='\n')) line[--n]=0;

    /* skip blank / comment */
    const char *p = line;
    while (*p==' ' || *p=='\t') p++;
    if (*p==0 || *p=='#') return ;

    /* split key=value */
    char *eq = strchr(line, '=');
    if (!eq) return ;
    *eq = 0;
    char *key = line;
    char *val = eq + 1;
    while (*val==' ' || *val=='\t') val++;
    while (*key==' ' || *key=='\t') key++;

    /* normalize key */
    str_lower(key);

    /* core */
    if (!strcmp(key, "proto")) {
        char v[8]={0}; strncpy(v,val,7); v[7]=0; str_lower(v);
        if      (!strcmp(v,"i2c"))  cfg->proto = proto_i2c;
        else if (!strcmp(v,"uart")) cfg->proto = proto_uart;
        else if (!strcmp(v,"gpio")) cfg->proto = proto_gpio;
        else if (!strcmp(v,"mqtt")) cfg->proto = proto_mqtt;   // NEW
    } else if (!strcmp(key, "mode")) {
        char v[8]={0}; strncpy(v,val,7); v[7]=0; str_lower(v);
        cfg->mode = (!strcmp(v,"stream")) ? sample_mode_stream : sample_mode_poll;
    } else if (!strcmp(key, "freq_hz")) {
        cfg->freq_hz = parse_f32(val);
    } else if (!strcmp(key, "name")) {
        /* up to 15 chars */
        size_t L = strlen(val); if (L > 15) L = 15;
        memcpy(cfg->name, val, L); cfg->name[L]=0;
    }
    /* NEW: remote_* for MQTT virtual sensors */
    else if (!strcmp(key, "remote_node")) {
        size_t L = strlen(val);
        if (L >= sizeof(cfg->remote_node)) L = sizeof(cfg->remote_node) - 1;
        memcpy(cfg->remote_node, val, L);
        cfg->remote_node[L] = 0;
    } else if (!strcmp(key, "remote_source")) {
        size_t L = strlen(val);
        if (L >= sizeof(cfg->remote_source)) L = sizeof(cfg->remote_source) - 1;
        memcpy(cfg->remote_source, val, L);
        cfg->remote_source[L] = 0;
    }

    /* i2c.* */
    else if (!strcmp(key,"i2c.sda"))         { cfg->i2c_sda = (int)parse_u32(val); }
    else if (!strcmp(key,"i2c.scl"))         { cfg->i2c_scl = (int)parse_u32(val); }
    else if (!strcmp(key,"i2c.addr"))        { cfg->i2c_addr= (uint8_t)parse_u32(val); }
    else if (!strcmp(key,"i2c.reg"))         { cfg->i2c_reg = (uint32_t)parse_u32(val); }
    else if (!strcmp(key,"i2c.reg_size"))    { cfg->i2c_reg_size = (uint8_t)parse_u32(val); }
    else if (!strcmp(key,"i2c.read_len"))    { cfg->i2c_read_len = (uint16_t)parse_u32(val); }
    else if (!strcmp(key,"i2c.restart"))     { cfg->i2c_restart = (parse_u32(val)!=0); }
    else if (!strcmp(key,"i2c.post_delay_ms")) { cfg->i2c_post_delay_ms = (uint16_t)parse_u32(val); }
    else if (!strcmp(key, "i2c.pre")) {
        // Parse bytes from 'val' into cfg->i2c_pre[]
        cfg->i2c_pre_len = 0;
        char bufcopy[160];
        strncpy(bufcopy, val, sizeof(bufcopy)-1);
        bufcopy[sizeof(bufcopy)-1] = 0;

        char *tok = strtok(bufcopy, " ,\t");
        while (tok && cfg->i2c_pre_len < sizeof(cfg->i2c_pre)) {
            cfg->i2c_pre[cfg->i2c_pre_len++] = (uint8_t)parse_u32(tok);
            tok = strtok(NULL, " ,\t");
        }
    }

    /* uart.* */
    else if (!strcmp(key,"uart.tx")) { cfg->uart_tx = (int)parse_u32(val); }
    else if (!strcmp(key,"uart.rx")) { cfg->uart_rx = (int)parse_u32(val); }
    else if (!strcmp(key,"uart.baud")) { cfg->uart_baud = parse_u32(val); }
    else if (!strcmp(key,"uart.bits")) { cfg->uart_bits = (uint8_t)parse_u32(val); }
    else if (!strcmp(key,"uart.parity")) { cfg->uart_parity = (val[0]? (char)toupper(val[0]) : 'N'); }
    else if (!strcmp(key,"uart.stop")) { cfg->uart_stop = (uint8_t)parse_u32(val); }
    else if (!strcmp(key,"uart.read_len")) { cfg->uart_read_len = (uint16_t)parse_u32(val); }
    else if (!strcmp(key,"uart.line_mode")) { cfg->uart_line_mode = (parse_u32(val)!=0); }
    else if (!strcmp(key,"uart.pre")) {
        // Parse bytes from 'val' into cfg->uart_pre[]
        cfg->uart_pre_len = 0;
        char bufcopy[160];
        strncpy(bufcopy, val, sizeof(bufcopy)-1);
        bufcopy[sizeof(bufcopy)-1] = 0;

        char *tok = strtok(bufcopy, " ,\t");
        while (tok && cfg->uart_pre_len < sizeof(cfg->uart_pre)) {
            cfg->uart_pre[cfg->uart_pre_len++] = (uint8_t)parse_u32(tok);
            tok = strtok(NULL, " ,\t");
        }
    }
    else if (!strcmp(key,"uart.post_delay_ms")) {
        cfg->uart_post_delay_ms = (uint16_t)parse_u32(val);
    }


     /* gpio.* */
    else if (!strcmp(key, "gpio.pin")) {
        cfg->gpio_pin = (int)parse_u32(val);
    }
    else if (!strcmp(key, "gpio.trig")) {
        cfg->gpio_trig = (int)parse_u32(val);
    }
    else if (!strcmp(key, "gpio.echo")) {
        cfg->gpio_echo = (int)parse_u32(val);
    }
    else if (!strcmp(key, "gpio.mode") || !strcmp(key, "gpio.method")) {
        // Treat gpio.mode and gpio.method as the same thing
        char v[16] = {0};
        strncpy(v, val, sizeof(v) - 1);
        v[sizeof(v) - 1] = 0;
        str_lower(v);

        if (!strcmp(v, "pulse")) {
            cfg->gpio_mode = "pulse";
        } else if (!strcmp(v, "onewire")) {
            cfg->gpio_mode = "onewire";
        } else if (!strcmp(v, "pwm")) {
            cfg->gpio_mode = "pwm";
        } else if (!strcmp(v, "counter")) {
            cfg->gpio_mode = "counter";
        } else {
            cfg->gpio_mode = "digital";   // default
        }
    }
    else if (!strcmp(key, "gpio.pull")) {
        char v[8] = {0};
        strncpy(v, val, sizeof(v) - 1);
        v[sizeof(v) - 1] = 0;
        str_lower(v);

        if (!strcmp(v, "up")) {
            cfg->gpio_pull = "up";
        } else if (!strcmp(v, "down")) {
            cfg->gpio_pull = "down";
        } else {
            cfg->gpio_pull = "off";   // default
        }
    }
    else if (!strcmp(key, "gpio.invert")) {
        cfg->gpio_invert = (int)parse_u32(val);
    }
    else if (!strcmp(key, "gpio.debounce_ms")) {
        cfg->gpio_debounce_ms = (int)parse_u32(val);
    }
    else if (!strcmp(key, "gpio.trig_us")) {
        cfg->gpio_trig_us = (int)parse_u32(val);
    }
    else if (!strcmp(key, "gpio.pulse_timeout_us")) {
        cfg->gpio_pulse_timeout_us = (int)parse_u32(val);
    }
return;
}

void cfg_parse_end(app_cfg_t *cfg) {
    s_in_block = 0;
    (void)cfg;
}
