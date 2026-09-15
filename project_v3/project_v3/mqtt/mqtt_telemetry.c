#include "mqtt/mqtt_telemetry.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "mqtt/sensor_bridge.h"

#ifndef MQTT_CLIENT_ID_MAX
#define MQTT_CLIENT_ID_MAX 64
#endif
#ifndef MQTT_TOPIC_MAX
#define MQTT_TOPIC_MAX 160
#endif
#ifndef MQTT_PAYLOAD_MAX
#define MQTT_PAYLOAD_MAX 1024
#endif
#ifndef MQTT_INQUEUE_MAX
#define MQTT_INQUEUE_MAX 6   // small ring buffer of pending inbound messages
#endif

// ---------- Globals ----------
static mqtt_client_t *s_client = NULL;
static ip_addr_t s_broker_ip;
static bool s_connected = false, s_have_dns = false;

static char s_client_id[MQTT_CLIENT_ID_MAX] = "pico";
static char s_node_id[MQTT_CLIENT_ID_MAX]   = "pico-node";

static mqtt_cfg_block_cb_t s_cfg_cb  = NULL;
static mqtt_cmd_cb_t       s_cmd_cb  = NULL;
static mqtt_node_msg_cb_t  s_node_cb = NULL;

static char TOPIC_CFG[MQTT_TOPIC_MAX];
static char TOPIC_CMD[MQTT_TOPIC_MAX];
static char TOPIC_BCAST_CFG[MQTT_TOPIC_MAX];
static char TOPIC_TEL[MQTT_TOPIC_MAX];
static char TOPIC_PRESENCE[MQTT_TOPIC_MAX];
static char TOPIC_HEART[MQTT_TOPIC_MAX];
static char TOPIC_NODE_IN[MQTT_TOPIC_MAX];

static const char B64TAB[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
bool mqtt_is_connected(void) {
    return s_connected;
}
// ---------- Base64 ----------
static int b64_encode(const uint8_t *in, size_t inlen, char *out, size_t outlen){
    size_t olen=4*((inlen+2)/3); if(outlen<olen+1) return -1; size_t i=0,o=0;
    while(i+2<inlen){uint32_t v=(in[i]<<16)|(in[i+1]<<8)|in[i+2];
        out[o++]=B64TAB[(v>>18)&63]; out[o++]=B64TAB[(v>>12)&63];
        out[o++]=B64TAB[(v>>6)&63];  out[o++]=B64TAB[v&63]; i+=3;}
    if(i+1<inlen){uint32_t v=(in[i]<<16)|(in[i+1]<<8);
        out[o++]=B64TAB[(v>>18)&63]; out[o++]=B64TAB[(v>>12)&63];
        out[o++]=B64TAB[(v>>6)&63];  out[o++]='=';}
    else if(i<inlen){uint32_t v=(in[i]<<16);
        out[o++]=B64TAB[(v>>18)&63]; out[o++]=B64TAB[(v>>12)&63];
        out[o++]='='; out[o++]='=';}
    out[o]=0; return (int)o;
}

// ---------- Topic helpers ----------
void mqtt_set_node_id(const char *node_id){
    snprintf(s_node_id,sizeof(s_node_id),"%s", (node_id&&*node_id)?node_id:"pico-node");
    snprintf(TOPIC_CFG,      sizeof TOPIC_CFG,      "pico/%s/config",   s_node_id);
    snprintf(TOPIC_CMD,      sizeof TOPIC_CMD,      "pico/%s/cmd",      s_node_id);
    snprintf(TOPIC_BCAST_CFG,sizeof TOPIC_BCAST_CFG,"pico/broadcast/config");
    snprintf(TOPIC_PRESENCE, sizeof TOPIC_PRESENCE, "pico/%s/presence", s_node_id);
    snprintf(TOPIC_HEART,    sizeof TOPIC_HEART,    "pico/%s/heartbeat",s_node_id);
    snprintf(TOPIC_NODE_IN,  sizeof TOPIC_NODE_IN,  "pico/%s/in",       s_node_id);
}

// ---------- Deferred inbound queue ----------
typedef enum {
    IN_CFG,
    IN_CMD,
    IN_BCAST_CFG,
    IN_NODE_IN,
    IN_REMOTE_LINE,   // new: remote sensor line messages
    IN_UNKNOWN
} in_kind_t;

typedef struct {
    in_kind_t kind;
    char      topic[MQTT_TOPIC_MAX];     // full topic
    char      payload[MQTT_PAYLOAD_MAX]; // textual payload
} in_msg_t;

static in_msg_t s_inq[MQTT_INQUEUE_MAX];
static volatile uint8_t s_in_head = 0; // write index (ISR/lwIP thread)
static volatile uint8_t s_in_tail = 0; // read index  (main thread)

// enqueue from lwIP callback (best effort, drop if full)
static void inq_push(in_kind_t kind, const char *topic, const char *data){
    uint8_t h = s_in_head;
    uint8_t n = (uint8_t)((h + 1) % MQTT_INQUEUE_MAX);
    if (n == s_in_tail) {
        // queue full: drop oldest (advance tail)
        s_in_tail = (uint8_t)((s_in_tail + 1) % MQTT_INQUEUE_MAX);
    }
    s_inq[h].kind = kind;
    snprintf(s_inq[h].topic, sizeof(s_inq[h].topic), "%s", topic ? topic : "");
    strncpy(s_inq[h].payload, data ? data : "", sizeof(s_inq[h].payload)-1);
    s_inq[h].payload[sizeof(s_inq[h].payload)-1] = 0;
    s_in_head = n;
}

// helper to parse "pico/<node>/sensor/<source>/<kind>"
static bool parse_sensor_topic(const char *topic,
                               char *node,  size_t node_sz,
                               char *src,   size_t src_sz,
                               char *kind,  size_t kind_sz)
{
    if (!topic) return false;
    if (strncmp(topic, "pico/", 5) != 0) return false;

    const char *p = topic + 5;
    const char *slash = strchr(p, '/');
    if (!slash) return false;

    size_t node_len = (size_t)(slash - p);
    if (node_len >= node_sz) node_len = node_sz - 1;
    memcpy(node, p, node_len);
    node[node_len] = 0;

    p = slash + 1;
    if (strncmp(p, "sensor/", 7) != 0) return false;
    p += 7;

    const char *slash2 = strchr(p, '/');
    if (!slash2) return false;

    size_t src_len = (size_t)(slash2 - p);
    if (src_len >= src_sz) src_len = src_sz - 1;
    memcpy(src, p, src_len);
    src[src_len] = 0;

    const char *kstart = slash2 + 1;
    if (!*kstart) return false;
    strncpy(kind, kstart, kind_sz - 1);
    kind[kind_sz - 1] = 0;

    return true;
}

// drain in main thread inside mqtt_poll()
static void inq_drain_call_user(void){
    while (s_in_tail != s_in_head) {
        in_msg_t m = s_inq[s_in_tail];
        s_in_tail = (uint8_t)((s_in_tail + 1) % MQTT_INQUEUE_MAX);

        switch (m.kind) {
            case IN_CFG:
                if (s_cfg_cb) s_cfg_cb(m.payload);
                break;
            case IN_CMD:
                if (s_cmd_cb) s_cmd_cb(m.payload);
                break;
            case IN_BCAST_CFG:
                if (s_cfg_cb) s_cfg_cb(m.payload);
                break;
            case IN_NODE_IN:
                if (s_node_cb) s_node_cb("unknown", m.payload);
                break;

            case IN_REMOTE_LINE: {
                char node[32], src[32], kind[16];
                if (parse_sensor_topic(m.topic, node, sizeof node,
                                       src, sizeof src,
                                       kind, sizeof kind)) {
                    // Only handle ".../line" for now
                    if (strcmp(kind, "line") == 0) {
                        uint32_t ts_ms = to_ms_since_boot(get_absolute_time());
                        sensor_bridge_on_remote_line(node, src, m.payload, ts_ms);
                    }
                }
            } break;

            case IN_UNKNOWN:
            default:
                // ignore
                break;
        }
    }
}

// ---------- MQTT callbacks (lwIP thread) ----------
static void mqtt_pub_cb(void *arg, err_t result){ (void)arg; if(result!=ERR_OK) printf("[MQTT] pub err=%d\n",result); }

static char s_last_topic[MQTT_TOPIC_MAX];

static void mqtt_incoming_cb(void *arg,const char *topic,u32_t tot_len){
    (void)arg; (void)tot_len;
    snprintf(s_last_topic,sizeof s_last_topic,"%s",topic?topic:"");
}

static void mqtt_incoming_data_cb(void *arg,const u8_t *data,u16_t len,u8_t flags){
    (void)arg;
    static char buf[MQTT_PAYLOAD_MAX];
    static size_t w=0;

    if(len>sizeof(buf)-1-w) len=(u16_t)(sizeof(buf)-1-w);
    memcpy(buf+w,data,len); w+=len;

    if(flags & MQTT_DATA_FLAG_LAST){
        buf[w]=0; w=0;

        if      (!strcmp(s_last_topic, TOPIC_CFG))
            inq_push(IN_CFG, s_last_topic, buf);
        else if (!strcmp(s_last_topic, TOPIC_CMD))
            inq_push(IN_CMD, s_last_topic, buf);
        else if (!strcmp(s_last_topic, TOPIC_BCAST_CFG))
            inq_push(IN_BCAST_CFG, s_last_topic, buf);
        else if (!strcmp(s_last_topic, TOPIC_NODE_IN))
            inq_push(IN_NODE_IN, s_last_topic, buf);
else {
    // debug: show all non-control topics
    //printf("[MQTT] incoming topic=%s payload='%s'\n", s_last_topic, buf);

    // Check if this looks like a sensor topic; for now we only handle /line
    if (strstr(s_last_topic, "/sensor/") != NULL &&
        strstr(s_last_topic, "/line")   != NULL) {
        //printf("[MQTT] classify as IN_REMOTE_LINE\n");
        inq_push(IN_REMOTE_LINE, s_last_topic, buf);
    } else {
        inq_push(IN_UNKNOWN, s_last_topic, buf);
    }
}
    }
}

static void mqtt_conn_cb(mqtt_client_t *c, void *arg, mqtt_connection_status_t status){
    (void)c; (void)arg;
    if(status==MQTT_CONNECT_ACCEPTED){
        s_connected=true; printf("[MQTT] connected\n");
        mqtt_subscribe(s_client,TOPIC_CFG,1,NULL,NULL);
        mqtt_subscribe(s_client,TOPIC_CMD,1,NULL,NULL);
        mqtt_subscribe(s_client,TOPIC_BCAST_CFG,1,NULL,NULL);
        mqtt_subscribe(s_client,TOPIC_NODE_IN,1,NULL,NULL);
        mqtt_publish_birth(true);
    } else {
        s_connected=false; printf("[MQTT] conn status=%d\n",status);
    }
}

// ---------- Public init/poll ----------
bool mqtt_init(const char *broker_host,uint16_t broker_port,const char *client_id,
                      mqtt_cfg_block_cb_t cfg_cb,mqtt_cmd_cb_t cmd_cb,mqtt_node_msg_cb_t node_cb){
    s_cfg_cb=cfg_cb; s_cmd_cb=cmd_cb; s_node_cb=node_cb;
    snprintf(s_client_id,sizeof s_client_id,"%s",(client_id&&*client_id)?client_id:"pico");
    mqtt_set_node_id(s_node_id);

    if(!s_client){
        s_client=mqtt_client_new();
        if(!s_client){ printf("[MQTT] new fail\n"); return false; }
        mqtt_set_inpub_callback(s_client,mqtt_incoming_cb,mqtt_incoming_data_cb,NULL);
    }

    err_t dns=dns_gethostbyname(broker_host,&s_broker_ip,NULL,NULL);
    if(dns==ERR_OK) s_have_dns=true;
    else if(dns==ERR_INPROGRESS) s_have_dns=false;
    else { printf("[MQTT] DNS err %d\n",dns); return false; }

    if(s_have_dns){
        struct mqtt_connect_client_info_t ci={0};
        ci.client_id=s_client_id; ci.keep_alive=30;
        err_t e=mqtt_client_connect(s_client,&s_broker_ip,broker_port,mqtt_conn_cb,NULL,&ci);
        if(e!=ERR_OK) printf("[MQTT] connect err=%d\n",e);
    }
    return true;
}

void mqtt_poll(void){
    // 1) reconnect if needed
    if(s_have_dns && !s_connected){
        static absolute_time_t next_try;
        if(to_ms_since_boot(get_absolute_time()) >= to_ms_since_boot(next_try)){
            next_try = make_timeout_time_ms(3000);
            struct mqtt_connect_client_info_t ci={0};
            ci.client_id=s_client_id; ci.keep_alive=30;
            err_t e=mqtt_client_connect(s_client,&s_broker_ip,1883,mqtt_conn_cb,NULL,&ci);
            if(e!=ERR_OK) printf("[MQTT] reconnect err=%d\n",e);
        }
    }
    // 2) **process inbound queue in main thread**
    inq_drain_call_user();
}

// ---------- Publishing / presence ----------
bool mqtt_pub_text(const char *topic,const char *fmt,...){
    if(!s_client||!s_connected) return false;
    char payload[MQTT_PAYLOAD_MAX];
    va_list ap; va_start(ap,fmt); vsnprintf(payload,sizeof payload,fmt,ap); va_end(ap);
    err_t e=mqtt_publish(s_client,topic,payload,(u16_t)strlen(payload),0,0,mqtt_pub_cb,NULL);
    return e==ERR_OK;
}

bool mqtt_pub_json_sensor(const char *sensor_name,const char *proto,
                                 const uint8_t *bytes,size_t len,uint32_t seq,uint32_t ts_ms){
    if(!s_client||!s_connected) return false;
    snprintf(TOPIC_TEL,sizeof TOPIC_TEL,"pico/%s/sensor/%s", s_node_id, sensor_name?sensor_name:"unnamed");
    char b64[MQTT_PAYLOAD_MAX/2]; int bl=b64_encode(bytes,len,b64,sizeof b64); if(bl<0) return false;
    char json[MQTT_PAYLOAD_MAX];
    int n=snprintf(json,sizeof json,"{\"ts\":%u,\"name\":\"%s\",\"proto\":\"%s\",\"seq\":%u,\"bytes_b64\":\"%s\"}",
                   ts_ms, sensor_name?sensor_name:"", proto?proto:"", seq, b64);
    if(n<=0||n>=(int)sizeof json) return false;
    err_t e=mqtt_publish(s_client,TOPIC_TEL,json,(u16_t)strlen(json),0,0,mqtt_pub_cb,NULL);
    return e==ERR_OK;
}

void mqtt_publish_birth(bool retained){
    if(!s_client||!s_connected) return;
    char birth[192];
    int n=snprintf(birth,sizeof birth,"{\"node\":\"%s\",\"cap\":[\"config\",\"telemetry\",\"node-msg\"]}", s_node_id);
    if(n<0) return;
    mqtt_publish(s_client,TOPIC_PRESENCE,birth,(u16_t)strlen(birth),1,retained?1:0,mqtt_pub_cb,NULL);
}

void mqtt_publish_heartbeat(uint32_t uptime_s){
    if(!s_client||!s_connected) return;
    char hb[64]; int n=snprintf(hb,sizeof hb,"{\"uptime_s\":%u}",uptime_s); if(n<0) return;
    mqtt_publish(s_client,TOPIC_HEART,hb,(u16_t)strlen(hb),0,0,mqtt_pub_cb,NULL);
}

bool mqtt_send_to_node(const char *to,const char *fmt,...){
    if(!s_client||!s_connected||!to||!*to) return false;
    char topic[MQTT_TOPIC_MAX]; snprintf(topic,sizeof topic,"pico/%s/in",to);
    char payload[MQTT_PAYLOAD_MAX]; va_list ap; va_start(ap,fmt); vsnprintf(payload,sizeof payload,fmt,ap); va_end(ap);
    err_t e=mqtt_publish(s_client,topic,payload,(u16_t)strlen(payload),0,0,mqtt_pub_cb,NULL);
    return e==ERR_OK;
}

bool mqtt_pub_bytes(const char *sensor_name,
                    const uint8_t *bytes, size_t len,
                    int qos, bool retain)
{
    // RAW topic removed: keep stub for compatibility.
    (void)sensor_name;
    (void)bytes;
    (void)len;
    (void)qos;
    (void)retain;
    // If you want to be loud during testing, uncomment:
    // printf("[MQTT] mqtt_pub_bytes() called but /raw topic is disabled\n");
    return false;
}


bool mqtt_pub_line(const char *sensor_name,
                          const char *line, int qos, bool retain)
{
    if (!s_client || !s_connected || !sensor_name || !line) return false;

    char topic[MQTT_TOPIC_MAX];
    // human-readable ascii line (same as serial)
    int tn = snprintf(topic, sizeof topic, "pico/%s/sensor/%s/line", s_node_id, sensor_name);
    if (tn <= 0 || tn >= (int)sizeof(topic)) return false;

    size_t len = strlen(line);
    if (qos < 0) qos = 0; 
    if (qos > 2) qos = 2;
    err_t e = mqtt_publish(s_client, topic, (const void*)line, (u16_t)len,
                           (u8_t)qos, retain ? 1 : 0, mqtt_pub_cb, NULL);
    return e == ERR_OK;
}

// ------------------- inter-node follow/unfollow helpers -------------------

static err_t sub_topic(const char *topic, u8_t qos) {
    if (!s_client || !s_connected || !topic || !*topic) return ERR_VAL;
    return mqtt_subscribe(s_client, topic, qos, NULL, NULL);
}
static err_t unsub_topic(const char *topic) {
    if (!s_client || !s_connected || !topic || !*topic) return ERR_VAL;
    return mqtt_unsubscribe(s_client, topic, NULL, NULL);
}

bool mqtt_follow_node_all(const char *other_node_id, int qos) {
    if (!other_node_id || !*other_node_id) return false;
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/#", other_node_id);
    if (qos < 0) qos = 0; 
    if (qos > 2) qos = 2;
    err_t e = sub_topic(topic, (u8_t)qos);
    if (e != ERR_OK) { printf("[MQTT] follow %s err=%d\n", topic, e); return false; }
    return true;
}

bool mqtt_follow_node_raw(const char *other_node_id, int qos)
{
    (void)other_node_id;
    (void)qos;
    // RAW topic removed: keep stub for compatibility.
    // printf("[MQTT] mqtt_follow_node_raw() ignored, /raw topic disabled\n");
    return false;
}
bool mqtt_follow_node_line(const char *other_node_id, int qos) {
    if (!other_node_id || !*other_node_id) return false;
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/+/line", other_node_id);
    if (qos < 0) qos = 0; 
    if (qos > 2) qos = 2;
    err_t e = sub_topic(topic, (u8_t)qos);
    if (e != ERR_OK) { printf("[MQTT] follow %s err=%d\n", topic, e); return false; }
    return true;
}

bool mqtt_unfollow_node_all(const char *other_node_id) {
    if (!other_node_id || !*other_node_id) return false;
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/#", other_node_id);
    err_t e = unsub_topic(topic);
    if (e != ERR_OK) { printf("[MQTT] unfollow %s err=%d\n", topic, e); return false; }
    return true;
}

bool mqtt_unfollow_node_raw(const char *other_node_id)
{
    (void)other_node_id;
    // RAW topic removed: keep stub for compatibility.
    // printf("[MQTT] mqtt_unfollow_node_raw() ignored, /raw topic disabled\n");
    return false;
}

bool mqtt_unfollow_node_line(const char *other_node_id) {
    if (!other_node_id || !*other_node_id) return false;
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/+/line", other_node_id);
    err_t e = unsub_topic(topic);
    if (e != ERR_OK) { printf("[MQTT] unfollow %s err=%d\n", topic, e); return false; }
    return true;
}

bool mqtt_follow_remote_source(const char *remote_node_id,
                               const char *remote_source,
                               const char *local_name)
{
    if (!remote_node_id || !*remote_node_id ||
        !remote_source || !*remote_source ||
        !local_name    || !*local_name) {
        printf("[MQTT] follow_remote_source: invalid args\n");
        return false;
    }

    // 1) register mapping in bridge
    if (!sensor_bridge_add_mapping(remote_node_id, remote_source, local_name)) {
        printf("[MQTT] follow_remote_source: bridge add failed\n");
        return false;
    }

    // 2) subscribe to that specific line topic
    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/%s/line", remote_node_id, remote_source);

    err_t e = sub_topic(topic, 0);   // qos 0 is fine for telemetry
    if (e != ERR_OK) {
        printf("[MQTT] follow_remote_source: subscribe err=%d for %s\n", e, topic);
        return false;
    }

    printf("[MQTT] follow_remote_source: %s/%s -> %s via %s\n",
           remote_node_id, remote_source, local_name, topic);
    return true;
}

bool mqtt_pub_event(const char *rule_name,
                    const char *event_type,
                    double value,
                    const char *msg,
                    uint32_t ts_ms)
{
    if (!s_client || !s_connected || !rule_name || !*rule_name)
        return false;

    char topic[MQTT_TOPIC_MAX];
    int tn = snprintf(topic, sizeof topic,
                      "pico/rule/%s/event",
                      rule_name);
    if (tn <= 0 || tn >= (int)sizeof(topic))
        return false;

    char payload[MQTT_PAYLOAD_MAX];
    int n = snprintf(payload, sizeof payload,
                     "ts=%u rule=%s type=%s value=%.3f msg=%s",
                     (unsigned)ts_ms,
                     rule_name,
                     event_type ? event_type : "log",
                     value,
                     msg ? msg : "");
    if (n <= 0 || n >= (int)sizeof(payload))
        return false;

    err_t e = mqtt_publish(s_client,
                           topic,
                           payload,
                           (u16_t)strlen(payload),
                           0,      // qos 0 is fine for logs; lighter on RAM
                           0,      // retain
                           mqtt_pub_cb,
                           NULL);
    if (e != ERR_OK) {
        printf("[MQTT] pub err=%d\n", e);
        return false;
    }
    return true;
}


bool mqtt_pub_cfg_status(const char *name, bool ok)
{
    if (!s_client || !s_connected || !name || !*name)
        return false;

    char topic[MQTT_TOPIC_MAX];
    int tn = snprintf(topic, sizeof topic,
                      "pico/%s/status/config", s_node_id);
    if (tn <= 0 || tn >= (int)sizeof(topic))
        return false;

    char payload[MQTT_PAYLOAD_MAX];
    int pn = snprintf(payload, sizeof payload,
                      "%s:%s",
                      ok ? "OK" : "FAIL",
                      name);
    if (pn <= 0 || pn >= (int)sizeof(payload))
        return false;

    err_t e = mqtt_publish(s_client,
                           topic,
                           payload,
                           (u16_t)strlen(payload),
                           0,      // qos 0 is fine
                           0,      // not retained
                           mqtt_pub_cb,
                           NULL);
    return e == ERR_OK;
}

bool mqtt_pub_rule_status(const char *name, bool ok)
{
    if (!s_client || !s_connected || !name || !*name)
        return false;

    char topic[MQTT_TOPIC_MAX];
    int tn = snprintf(topic, sizeof topic,
                      "pico/%s/status/rule", s_node_id);
    if (tn <= 0 || tn >= (int)sizeof(topic))
        return false;

    char payload[MQTT_PAYLOAD_MAX];
    int pn = snprintf(payload, sizeof payload,
                      "%s:%s",
                      ok ? "OK" : "FAIL",
                      name);
    if (pn <= 0 || pn >= (int)sizeof(payload))
        return false;

    err_t e = mqtt_publish(s_client,
                           topic,
                           payload,
                           (u16_t)strlen(payload),
                           0,      // qos 0
                           0,      // not retained
                           mqtt_pub_cb,
                           NULL);
    return e == ERR_OK;
}

