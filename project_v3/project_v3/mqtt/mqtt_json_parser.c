// mqtt_json_parser.c
#include "mqtt_json_parser.h"
#include "jsmn.h"
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { char key[64]; char val[256]; } kv_pair_t;

#define JSON_BUF_SZ     1024
#define JSON_FLAT_MAX   64

static char s_norm[JSON_BUF_SZ];
static char s_work[JSON_BUF_SZ];
static kv_pair_t s_flat[JSON_FLAT_MAX];


/* -------- helpers: sanitizers / normalizers -------- */

static void strip_trailing_commas(char *s) {
    bool in_str=false, esc=false;
    size_t w=0;
    for (size_t i=0; s[i]; i++) {
        char c = s[i];
        if (in_str) {
            if (esc) { esc=false; s[w++]='\\'; s[w++]=c; continue; }
            if (c=='\\') { esc=true; continue; }
            if (c=='"') in_str=false;
            s[w++]=c; continue;
        }
        if (c=='"') { in_str=true; s[w++]=c; continue; }
        if (c==',') {
            size_t j=i+1;
            while (s[j]==' '||s[j]=='\t'||s[j]=='\n') j++; // '\r' already removed
            if (s[j]=='}' || s[j]==']') continue;  // drop trailing comma
        }
        s[w++]=c;
    }
    s[w]=0;
}

static const char* normalize_json(const char *in, char *tmp, size_t tmp_sz) {
    if (!in || !tmp || tmp_sz < 8) return in;
    const unsigned char *u = (const unsigned char*)in;

    // UTF-8 BOM
    if (u[0]==0xEF && u[1]==0xBB && u[2]==0xBF) in += 3, u=(const unsigned char*)in;

    // UTF-16 LE BOM
    if (u[0]==0xFF && u[1]==0xFE) {
        size_t w=0;
        for (size_t i=2; in[i] || in[i+1]; i+=2) { if (w+1>=tmp_sz) break; tmp[w++]=in[i]; }
        tmp[w]=0; return tmp;
    }
    // UTF-16 BE BOM
    if (u[0]==0xFE && u[1]==0xFF) {
        size_t w=0;
        for (size_t i=3; in[i-1] || in[i]; i+=2) { if (w+1>=tmp_sz) break; tmp[w++]=in[i-1]; }
        tmp[w]=0; return tmp;
    }
    // Heuristic: UTF-16 LE without BOM
    if (u[0]=='{' && u[1]==0x00) {
        size_t w=0;
        for (size_t i=0; in[i] || in[i+1]; i+=2) { if (w+1>=tmp_sz) break; tmp[w++]=in[i]; }
        tmp[w]=0; return tmp;
    }

    // Strip control chars; drop '\r' explicitly
    size_t w=0;
    for (size_t i=0; in[i]!=0; i++) {
        unsigned char c=(unsigned char)in[i];
        if (c == '\r') continue;                       // drop CR
        if (c<0x20 && c!='\n' && c!='\t' && c!=' ') continue;
        if (w+1>=tmp_sz) break;
        tmp[w++]=in[i];
    }
    tmp[w]=0;
    return tmp;
}

/* -------- helpers: token/string utilities -------- */

static void slice_to_str(const char *src, int start, int end, char *out, size_t out_sz) {
    size_t w = 0;
    for (int i = start; i < end; i++) {
        if (w + 1 >= out_sz) break;
        if (src[i] == '\\' && i + 1 < end) {
            char next = src[i+1];
            switch (next) {
                case '"':  out[w++] = '"';  i++; break;
                case '\\': out[w++] = '\\'; i++; break;
                case '/':  out[w++] = '/';  i++; break;
                case 'b':  out[w++] = '\b'; i++; break;
                case 'f':  out[w++] = '\f'; i++; break;
                case 'n':  out[w++] = '\n'; i++; break;
                case 'r':  out[w++] = '\r'; i++; break;
                case 't':  out[w++] = '\t'; i++; break;
                default: out[w++] = '\\'; break;
            }
        } else {
            out[w++] = src[i];
        }
    }
    out[w] = '\0';
}
static void tok_to_str(const char *json, const jsmntok_t *tok, char *out, size_t out_sz) {
    slice_to_str(json, tok->start, tok->end, out, out_sz);
}

/* Find the end index (exclusive) of the top-level JSON object starting at '{' */
static int find_json_end(const char *s) {
    int i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (s[i] != '{') return -1;

    int depth = 0;
    bool in_str = false, esc = false;
    for (; s[i]; i++) {
        char c = s[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (in_str) {
            if (c == '"')  { in_str = false; continue; }
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') { depth++; continue; }
        if (c == '}') {
            depth--;
            if (depth == 0) return i + 1; // end exclusive
        }
    }
    return -1;
}

static int stringify_array_as_spaces(const char *json, const jsmntok_t *toks, int i, int count,
                                     char *out, size_t out_sz) {
    if (toks[i].type != JSMN_ARRAY) return i;
    int items = toks[i].size;
    int j = i + 1;
    size_t used = 0;
    for (int k = 0; k < items; k++) {
        char tmp[64]; tok_to_str(json, &toks[j], tmp, sizeof(tmp));
        size_t L = strlen(tmp);
        if (used + L + (k ? 1 : 0) + 1 > out_sz) { out[used] = '\0'; return j + 1; }
        if (k) out[used++] = ' ';
        memcpy(out + used, tmp, L); used += L;
        out[used] = '\0';
        j++;
    }
    return j;
}

// Walk an object by its end boundary instead of relying on toks[i].size
static int flatten_object(const char *json, const jsmntok_t *toks, int i, int count,
                          const char *prefix, kv_pair_t *out, int *out_n, int out_cap) {
    if (i < 0 || i >= count) return i;
    if (toks[i].type != JSMN_OBJECT) return i;

    const int obj_end = toks[i].end;   // stop when next token starts >= obj_end
    int j = i + 1;

    while (j + 1 < count) {
        if (toks[j].start >= obj_end) break;    // end of this object
        if (toks[j].type != JSMN_STRING) { j++; continue; } // malformed safety

        const jsmntok_t *k = &toks[j++];
        if (j >= count) break;   // no value
        const jsmntok_t *v = &toks[j];

        // dotted key
        char keybuf[64]; tok_to_str(json, k, keybuf, sizeof(keybuf));
        char fullkey[96];
        if (prefix && prefix[0]) snprintf(fullkey, sizeof(fullkey), "%s.%s", prefix, keybuf);
        else                     snprintf(fullkey, sizeof(fullkey), "%s", keybuf);

        if (v->type == JSMN_OBJECT) {
            j = flatten_object(json, toks, j, count, fullkey, out, out_n, out_cap);
        } else if (v->type == JSMN_ARRAY) {
            if (*out_n < out_cap) {
                strncpy(out[*out_n].key, fullkey, sizeof(out[*out_n].key)-1);
                out[*out_n].key[sizeof(out[*out_n].key)-1] = '\0';
                out[*out_n].val[0] = '\0';
                j = stringify_array_as_spaces(json, toks, j, count,
                                              out[*out_n].val, sizeof(out[*out_n].val));
                (*out_n)++;
            } else {
                j = stringify_array_as_spaces(json, toks, j, count, (char[1]){0}, 1);
            }
        } else {
            if (*out_n < out_cap) {
                char vbuf[128]; tok_to_str(json, v, vbuf, sizeof(vbuf));
                if (v->type == JSMN_PRIMITIVE) {
                    if (strncmp(vbuf, "true", 4) == 0)      strcpy(vbuf, "1");
                    else if (strncmp(vbuf, "false", 5) == 0) strcpy(vbuf, "0");
                }
                strncpy(out[*out_n].key, fullkey, sizeof(out[*out_n].key)-1);
                out[*out_n].key[sizeof(out[*out_n].key)-1] = '\0';
                strncpy(out[*out_n].val, vbuf, sizeof(out[*out_n].val)-1);
                out[*out_n].val[sizeof(out[*out_n].val)-1] = '\0';
                (*out_n)++;
            }
            j++; // consume value
        }
    }
    return j;
}

/* -------- small helpers for object navigation -------- */

static int find_value_in_object(const char *json,
                                const jsmntok_t *toks, int obj_idx, int count,
                                const char *key) {
    if (obj_idx < 0 || obj_idx >= count) return -1;
    if (toks[obj_idx].type != JSMN_OBJECT) return -1;

    int obj_end = toks[obj_idx].end;
    int j = obj_idx + 1;

    while (j + 1 < count && toks[j].start < obj_end) {
        if (toks[j].type != JSMN_STRING) { j++; continue; }
        char kbuf[32]; tok_to_str(json, &toks[j], kbuf, sizeof(kbuf));
        j++;
        if (j >= count) break;
        const jsmntok_t *v = &toks[j];

        if (strcmp(kbuf, key) == 0) {
            return j;  // index of value
        }

        int val_end = v->end;
        j++;
        while (j < count && toks[j].start < val_end) j++;
    }
    return -1;
}

static int skip_token_tree(const jsmntok_t *toks, int i, int count) {
    if (i < 0 || i >= count) return i;
    int end = toks[i].end;
    int j = i + 1;
    while (j < count && toks[j].start < end) j++;
    return j;
}

/* -------- append helpers for building inline string -------- */

static int append_str(char *out, size_t out_sz, int used, const char *s) {
    size_t rem = (used >= 0 && (size_t)used < out_sz) ? (out_sz - (size_t)used) : 0;
    if (rem == 0) return -1;
    int w = snprintf(out + used, rem, "%s", s);
    if (w < 0 || (size_t)w >= rem) return -1;
    return used + w;
}

static int append_kv(char *out, size_t out_sz, int used,
                     const char *key, const char *val) {
    size_t rem = (used >= 0 && (size_t)used < out_sz) ? (out_sz - (size_t)used) : 0;
    if (rem == 0) return -1;
    int w = snprintf(out + used, rem, "|%s=%s", key, val);
    if (w < 0 || (size_t)w >= rem) return -1;
    return used + w;
}

static int append_cfg_block_from_flat(const kv_pair_t *flat, int flat_n,
                                      char *out, size_t out_sz, int used) {
    used = append_str(out, out_sz, used, "BEGINCFG");
    if (used < 0) return -1;
    for (int i = 0; i < flat_n; i++) {
        used = append_kv(out, out_sz, used, flat[i].key, flat[i].val);
        if (used < 0) return -1;
    }
    used = append_str(out, out_sz, used, "|ENDCFG");
    return used;
}

static int append_rule_block_from_object(const char *json,
                                         const jsmntok_t *toks, int obj_idx, int count,
                                         char *out, size_t out_sz, int used) {
    used = append_str(out, out_sz, used, "BEGINRULE");
    if (used < 0) return -1;

    // name
    int v_idx = find_value_in_object(json, toks, obj_idx, count, "name");
    if (v_idx >= 0) {
        char buf[64]; tok_to_str(json, &toks[v_idx], buf, sizeof(buf));
        used = append_kv(out, out_sz, used, "name", buf);
        if (used < 0) return -1;
    }

    // source
    v_idx = find_value_in_object(json, toks, obj_idx, count, "source");
    if (v_idx >= 0) {
        char buf[64]; tok_to_str(json, &toks[v_idx], buf, sizeof(buf));
        used = append_kv(out, out_sz, used, "source", buf);
        if (used < 0) return -1;
    }

    // calc: array of strings, each becomes its own calc=...
    v_idx = find_value_in_object(json, toks, obj_idx, count, "calc");
    if (v_idx >= 0 && toks[v_idx].type == JSMN_ARRAY) {
        int items = toks[v_idx].size;
        int j = v_idx + 1;
        for (int k = 0; k < items; k++) {
            if (j >= count) break;
            char buf[256]; tok_to_str(json, &toks[j], buf, sizeof(buf));
            used = append_kv(out, out_sz, used, "calc", buf);
            if (used < 0) return -1;
            j++;
        }
    }

    // when
    v_idx = find_value_in_object(json, toks, obj_idx, count, "when");
    if (v_idx >= 0) {
        char buf[256]; tok_to_str(json, &toks[v_idx], buf, sizeof(buf));
        used = append_kv(out, out_sz, used, "when", buf);
        if (used < 0) return -1;
    }

    // action
    v_idx = find_value_in_object(json, toks, obj_idx, count, "action");
    if (v_idx >= 0) {
        char buf[256]; tok_to_str(json, &toks[v_idx], buf, sizeof(buf));
        
        // Escape any '|' characters in the action string so they don't break the rule parser
        char escaped[512] = {0};
        int wi = 0;
        for (int ri = 0; buf[ri] && wi < sizeof(escaped) - 2; ri++) {
            if (buf[ri] == '|') {
                escaped[wi++] = '\\';
                escaped[wi++] = '|';
            } else {
                escaped[wi++] = buf[ri];
            }
        }
        escaped[wi] = 0;

        used = append_kv(out, out_sz, used, "action", escaped);
        if (used < 0) return -1;
    }

    used = append_str(out, out_sz, used, "|ENDRULE");
    return used;
}

/* -------- public API -------- */

int mqtt_json_try_convert_to_inline(const char *payload, char *out, size_t out_sz) {
    if (!payload || !out || out_sz == 0) return -1;

    //printf("[JSON] raw first='%c' (0x%02X)\n",
           //payload[0] ? payload[0] : '?',
           //(unsigned char)(payload[0]));

    // Normalize + trailing-comma sanitize into work buffer

    const char *src0 = normalize_json(payload, s_norm, sizeof(s_norm));

    strncpy(s_work, src0, sizeof(s_work)-1);
    s_work[sizeof(s_work)-1] = 0;
    strip_trailing_commas(s_work);

    //printf("[JSON] normalized preview: %.*s\n", 120, work);


    const char *p = s_work;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '{') {
        printf("[JSON] not starting with '{' after trim, char=0x%02X\n",
               (unsigned char)*p);
        return 0;  // not JSON
    }

    int end = find_json_end(p);
    if (end < 0) {
        printf("[JSON] find_json_end failed\n");
        return -2;
    }
    int span_len = end;

    //printf("[JSON] span_len=%d (ends with '%c' 0x%02X)\n",span_len,p[span_len-1],(unsigned char)p[span_len-1]);

    // Grow-until-success parse
    int cap = 128;
    jsmntok_t *toks = NULL;
    int r = -1;

    while (cap <= 4096) {
        toks = (jsmntok_t*)malloc(sizeof(jsmntok_t) * (size_t)cap);
        if (!toks) {
            printf("[JSON] malloc failed at cap=%d\n", cap);
            return -2;
        }

        jsmn_parser jp;
        jsmn_init(&jp);
        r = jsmn_parse(&jp, p, (size_t)span_len, toks, (unsigned int)cap);

        //printf("[JSON] jsmn_parse cap=%d -> %d\n", cap, r);

        if (r >= 0) break;                 // success
        if (r == JSMN_ERROR_NOMEM) { free(toks); cap *= 2; continue; }

        printf("[JSON] jsmn error=%d (INVALID/PART)\n", r);
        free(toks);
        return -2; // INVALID or PART
    }

    if (r < 0 || toks[0].type != JSMN_OBJECT) {
        printf("[JSON] root token bad: r=%d type=%d\n",
               r, (r>=0)?toks[0].type:-1);
        free(toks);
        return -2;
    }

    int used = 0;
    bool any_blocks = false;
    int count = r;

    /* Check if this is the "combined" form: { "configs": [...], "rules": [...] } */
    int root_end = toks[0].end;
    int idx = 1;

    while (idx + 1 < count && toks[idx].start < root_end) {
        if (toks[idx].type != JSMN_STRING) {
            idx++;
            continue;
        }

        char kbuf[32]; tok_to_str(p, &toks[idx], kbuf, sizeof(kbuf));
        idx++;
        if (idx >= count) break;
        int v_idx = idx;
        const jsmntok_t *v = &toks[v_idx];

        //printf("[JSON] root key='%s' type=%d\n", kbuf, v->type);

        if (strcmp(kbuf, "configs") == 0 && v->type == JSMN_ARRAY) {
            int arr_end = v->end;
            int j = v_idx + 1;
            //int items = v->size;

            //printf("[JSON] configs array size=%d arr_end=%d\n", items, arr_end);

            int cfg_count = 0;
            for (int n = 0; j < count && toks[j].start < arr_end; n++) {
                //printf("[JSON]  cfg item n=%d j=%d type=%d start=%d end=%d\n", n, j, toks[j].type, toks[j].start, toks[j].end);

                if (toks[j].type != JSMN_OBJECT) {
                    printf("[JSON]   cfg item j=%d is not OBJECT, skipping tree\n", j);
                    j = skip_token_tree(toks, j, count);
                    continue;
                }

                int flat_n = 0;
                flatten_object(p, toks, j, count, "", s_flat, &flat_n, JSON_FLAT_MAX);

                // printf("[JSON]   cfg item j=%d flat_n=%d\n", j, flat_n);
                //for (int ff = 0; ff < flat_n; ff++) {
                    // printf("[JSON]     cfg kv[%d]: %s=%s\n", ff, flat[ff].key, flat[ff].val);
               // }

                used = append_cfg_block_from_flat(s_flat, flat_n, out, out_sz, used);
                if (used < 0) {
                    printf("[JSON] append_cfg_block_from_flat failed\n");
                    free(toks);
                    return -2;
                }
                any_blocks = true;
                cfg_count++;

                j = skip_token_tree(toks, j, count);
                // printf("[JSON]   next j after cfg=%d\n", j);
            }
            printf("[JSON] configs processed: %d\n", cfg_count);
            idx = skip_token_tree(toks, v_idx, count);
            //printf("[JSON] idx after configs tree=%d\n", idx);
            continue;
        }


        if (strcmp(kbuf, "rules") == 0 && v->type == JSMN_ARRAY) {
            int arr_end = v->end;
            int j = v_idx + 1;
            //int items = v->size;

            //printf("[JSON] rules array size=%d arr_end=%d\n", items, arr_end);

            int rule_count = 0;
            for (int n = 0; j < count && toks[j].start < arr_end; n++) {
                //printf("[JSON]  rule item n=%d j=%d type=%d start=%d end=%d\n",n, j, toks[j].type, toks[j].start, toks[j].end);

                if (toks[j].type != JSMN_OBJECT) {
                    printf("[JSON]   rule item j=%d is not OBJECT, skipping tree\n", j);
                    j = skip_token_tree(toks, j, count);
                    continue;
                }

                used = append_rule_block_from_object(p, toks, j, count, out, out_sz, used);
                if (used < 0) {
                    printf("[JSON] append_rule_block_from_object failed\n");
                    free(toks);
                    return -2;
                }
                any_blocks = true;
                rule_count++;

                j = skip_token_tree(toks, j, count);
                //printf("[JSON]   next j after rule=%d\n", j);
            }
            printf("[JSON] rules processed: %d\n", rule_count);
            idx = skip_token_tree(toks, v_idx, count);
            //printf("[JSON] idx after rules tree=%d\n", idx);
            continue;
        }


        // Single-object compatibility: look for "type":"config"/"rule"
        if (strcmp(kbuf, "type") == 0 && v->type == JSMN_STRING) {
            char tbuf[16]; tok_to_str(p, v, tbuf, sizeof(tbuf));
            printf("[JSON] single-object type='%s'\n", tbuf);
            if (strcmp(tbuf, "config") == 0) {
                kv_pair_t flat[64];
                int flat_n = 0;
                flatten_object(p, toks, 0, count, "", flat, &flat_n, 64);
                used = append_cfg_block_from_flat(flat, flat_n, out, out_sz, used);
                any_blocks = (used >= 0);
            } else if (strcmp(tbuf, "rule") == 0) {
                used = append_rule_block_from_object(p, toks, 0, count, out, out_sz, used);
                any_blocks = (used >= 0);
            }
            free(toks);
            if (!any_blocks || used < 0) {
                printf("[JSON] single-object – no blocks or error\n");
                return -2;
            }
            printf("[JSON] single-object inline: %.*s\n",
                   used > 200 ? 200 : used, out);
            return used;
        }

        // Skip this value's subtree
        idx = skip_token_tree(toks, v_idx, count);
    }

    free(toks);

    if (!any_blocks || used <= 0) {
        // JSON was valid but not a known schema
        printf("[JSON] valid JSON but no configs/rules recognised, any_blocks=%d used=%d\n",
               any_blocks ? 1 : 0, used);
        return 0;
    }

    //printf("[JSON] combined inline: %.*s\n", used > 200 ? 200 : used, out);
    //printf("[JSON] returning len=%d\n", used);
    return used;
}
