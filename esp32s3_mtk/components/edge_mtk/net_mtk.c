// net_mtk.c
// Wi-Fi and MQTT for the ESP32-S3 on µT-Kernel 3 SMP (D1 task 30): MQTTTask
// and the shared core's mqtt_telemetry.h interface. Replaces the Arduino layer
// (libraries/EdgeS3/src/mqtt_s3.cpp) with the same topics, payloads, console
// messages and limits, on esp_wifi and esp-mqtt (espressif/mqtt, version
// locked in dependencies.lock).
//
// Ownership (MTK_REDESIGN.md section 5): MQTTTask, on core 0, owns Wi-Fi and
// the MQTT client and is the only task that calls them, except WIFISCAN and
// WIFIDROP, which ControlTask runs on request. IDF's Wi-Fi, lwIP and esp-mqtt
// work in their own tasks, which the course port's shim creates as µT-Kernel
// tasks pinned to core 0. Their event handlers here only record state, set or
// clear µT-Kernel event flags (net_flg) and copy inbound messages into
// sched_mbf, so configuration is only ever applied by SchedulerTask. Model
// chunks go straight to the model store, as on the Pico. Everything the core
// publishes, and every subscription it asks for, goes through pub_mbf.
//
// Limits, as before: an inbound payload longer than SCHED_TEXT_MAX - 1 bytes
// is truncated with a warning; a full sched_mbf drops the new message and
// logs it; while the broker is unreachable, publishing returns false and
// nothing is queued. esp-mqtt reconnects every 5 s.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "hal/hal.h"
#include "mqtt/mqtt_model_transfer.h"
#include "mqtt/mqtt_telemetry.h"
#include "mqtt/sensor_bridge.h"
#include "runtime_mtk.h"

#define MQTT_BUF_SIZE   (SCHED_TEXT_MAX + 512)
#define MAX_FOLLOWS     8

static char s_host[64];
static uint16_t s_port = 1883;
static char s_node[64] = "pico-node";
static char s_client_id[64] = "pico";
static volatile bool s_configured;          // set by mqtt_init() in SchedulerTask

static char s_follows[MAX_FOLLOWS][PUB_TOPIC_MAX];   // re-subscribed on every connect
static volatile int s_nfollows;

static esp_mqtt_client_handle_t s_client;
static volatile bool s_wifi_started;
static char s_ip[16] = "0.0.0.0";
static volatile int s_wifi_reason = -1;     // last disconnect reason, reported once
static volatile bool s_wifi_reason_new;
static volatile uint32_t s_wifi_hold_until; // WIFIDROP: no reconnect before this
static volatile uint32_t s_wifi_down_ms;    // when the last disconnect event arrived (0: none pending)

// ---------- the core's interface (called from SchedulerTask) ----------

static UINT net_flags(void)
{
    T_RFLG r;
    return (net_flg > 0 && tk_ref_flg(net_flg, &r) == E_OK) ? r.flgptn : 0;
}

bool mqtt_is_connected(void)
{
    return (net_flags() & NET_MQTT_UP) != 0;
}

static bool enqueue(uint8_t op, const char *topic, const char *payload, size_t len, int qos, bool retain)
{
    if (op == PUB_PUBLISH && !mqtt_is_connected()) return false;
    static pub_msg_t m;                     // callers all run in SchedulerTask
    m.op = op;
    snprintf(m.topic, sizeof m.topic, "%s", topic);
    if (len >= sizeof m.payload) len = sizeof m.payload - 1;
    memcpy(m.payload, payload, len);
    m.payload[len] = 0;
    m.len = (uint16_t)len;
    m.qos = (uint8_t)qos;
    m.retain = retain;
    if (!runtime_send(pub_mbf, &m, sizeof m)) {
        static uint32_t s_last_warn;
        if (hal_time_ms() - s_last_warn >= 1000) {
            printf("[MQTT] publish queue full, dropped %s\n", topic);
            s_last_warn = hal_time_ms();
        }
        return false;
    }
    return true;
}

bool mqtt_init(const char *broker_host, uint16_t broker_port, const char *client_id,
               mqtt_cfg_block_cb_t cfg_cb, mqtt_cmd_cb_t cmd_cb, mqtt_node_msg_cb_t node_cb)
{
    runtime_set_mqtt_handlers(cfg_cb, cmd_cb, node_cb);
    if (broker_host && *broker_host) snprintf(s_host, sizeof s_host, "%s", broker_host);
    if (broker_port) s_port = broker_port;
    snprintf(s_client_id, sizeof s_client_id, "%s", (client_id && *client_id) ? client_id : "pico");
    s_configured = s_host[0] != 0;
    return s_configured;
}

void mqtt_set_node_id(const char *node_id)
{
    snprintf(s_node, sizeof s_node, "%s", (node_id && *node_id) ? node_id : "pico-node");
}

void mqtt_poll(void) {}                     // MQTTTask does the work

bool mqtt_pub_text(const char *topic, const char *fmt, ...)
{
    char payload[PUB_PAYLOAD_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(payload, sizeof payload, fmt, ap);
    va_end(ap);
    return enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

bool mqtt_pub_bytes(const char *sensor_name, const uint8_t *bytes, size_t len, int qos, bool retain)
{
    (void)sensor_name; (void)bytes; (void)len; (void)qos; (void)retain;
    return false;                            // the /raw topic is disabled in the reference too
}

bool mqtt_pub_line(const char *sensor_name, const char *line, int qos, bool retain)
{
    if (!sensor_name || !*sensor_name || !line) return false;
    char topic[PUB_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/%s/line", s_node, sensor_name);
    return enqueue(PUB_PUBLISH, topic, line, strlen(line), qos, retain);
}

bool mqtt_pub_event(const char *rule_name, const char *event_type, double value,
                    const char *msg, uint32_t ts_ms)
{
    if (!rule_name || !*rule_name) return false;
    char topic[PUB_TOPIC_MAX], payload[PUB_PAYLOAD_MAX];
    snprintf(topic, sizeof topic, "pico/rule/%s/event", rule_name);
    snprintf(payload, sizeof payload, "ts=%u rule=%s type=%s value=%.3f msg=%s", (unsigned)ts_ms,
             rule_name, event_type ? event_type : "log", value, msg ? msg : "");
    return enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

static bool pub_status(const char *kind, const char *name, bool ok)
{
    if (!name || !*name) return false;
    char topic[PUB_TOPIC_MAX], payload[64];
    snprintf(topic, sizeof topic, "pico/%s/status/%s", s_node, kind);
    snprintf(payload, sizeof payload, "%s:%s", ok ? "OK" : "FAIL", name);
    return enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

bool mqtt_pub_cfg_status(const char *name, bool ok)  { return pub_status("config", name, ok); }
bool mqtt_pub_rule_status(const char *name, bool ok) { return pub_status("rule", name, ok); }

void mqtt_publish_birth(bool retained)
{
    char topic[PUB_TOPIC_MAX], payload[128];
    snprintf(topic, sizeof topic, "pico/%s/presence", s_node);
    snprintf(payload, sizeof payload, "{\"node\":\"%s\",\"cap\":[\"config\",\"telemetry\",\"node-msg\"]}", s_node);
    enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 1, retained);
}

void mqtt_publish_heartbeat(uint32_t uptime_s)
{
    char topic[PUB_TOPIC_MAX], payload[48];
    snprintf(topic, sizeof topic, "pico/%s/heartbeat", s_node);
    snprintf(payload, sizeof payload, "{\"uptime_s\":%u}", (unsigned)uptime_s);
    enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

bool mqtt_follow_remote_source(const char *other_node_id, const char *remote_source,
                               const char *local_alias)
{
    if (!other_node_id || !remote_source || !local_alias) {
        printf("[MQTT] follow_remote_source: invalid args\n");
        return false;
    }
    if (!sensor_bridge_add_mapping(other_node_id, remote_source, local_alias)) {
        printf("[MQTT] follow_remote_source: bridge add failed\n");
        return false;
    }
    char topic[PUB_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/%s/line", other_node_id, remote_source);
    bool known = false;
    for (int i = 0; i < s_nfollows; i++) known = known || strcmp(s_follows[i], topic) == 0;
    if (!known && s_nfollows < MAX_FOLLOWS) {
        snprintf(s_follows[s_nfollows], PUB_TOPIC_MAX, "%s", topic);
        __atomic_add_fetch(&s_nfollows, 1, __ATOMIC_RELEASE);
    }
    enqueue(PUB_SUBSCRIBE, topic, "", 0, 1, false);   // also subscribed again on every connect
    printf("[MQTT] follow_remote_source: %s/%s -> %s via %s\n", other_node_id, remote_source, local_alias, topic);
    return true;
}

// ---------- inbound routing (runs in the esp-mqtt task) ----------

static void route_message(const char *topic, const char *bytes, int length)
{
    static sched_msg_t msg;                 // only the esp-mqtt task uses it
    char own[80];
    int own_len = snprintf(own, sizeof own, "pico/%s/", s_node);

    msg.kind = 0;
    if (strncmp(topic, own, own_len) == 0) {
        const char *rest = topic + own_len;
        if (!strcmp(rest, "config"))      msg.kind = SCHED_MQTT_CONFIG;
        else if (!strcmp(rest, "cmd"))    msg.kind = SCHED_MQTT_CMD;
        else if (!strcmp(rest, "in"))     msg.kind = SCHED_MQTT_NODE;
        else if (!strcmp(rest, "model/params")) {
            printf("[MQTT] Norm params on: %s (%d bytes)\n", topic, length);
            msg.kind = SCHED_MODEL_PARAMS;   // inference state belongs to SchedulerTask
        } else if (!strncmp(rest, "model/", 6)) {
            // binary chunks go straight to the model store, as on the Pico
            printf("[MQTT] Model data on: %s (%d bytes)\n", topic, length);
            model_transfer_on_data(topic, (const uint8_t *)bytes, (size_t)length);
            return;
        }
        // own status, sensor, presence and heartbeat echoes are dropped
    } else if (!strcmp(topic, "pico/broadcast/config")) {
        msg.kind = SCHED_MQTT_CONFIG;
    } else if (strstr(topic, "/sensor/") && strlen(topic) > 5 &&
               !strcmp(topic + strlen(topic) - 5, "/line")) {
        msg.kind = SCHED_REMOTE_LINE;       // a followed node's sensor (D-10)
    }
    if (!msg.kind) return;

    if (length > SCHED_TEXT_MAX - 1) {
        printf("[MQTT] payload of %d bytes on %s truncated to %d\n", length, topic, SCHED_TEXT_MAX - 1);
        length = SCHED_TEXT_MAX - 1;
    }
    snprintf(msg.topic, sizeof msg.topic, "%s", topic);
    memcpy(msg.text, bytes, (size_t)length);
    msg.text[length] = 0;
    msg.len = (uint16_t)length;
    if (!runtime_send(sched_mbf, &msg, sizeof msg))
        printf("[MQTT] inbound queue full, dropped message on %s\n", topic);
}

// esp-mqtt delivers a payload larger than its buffer in pieces; they are put
// back together here (in the esp-mqtt task) before routing.
static char s_rx_topic[PUB_TOPIC_MAX];
static char s_rx_buf[MQTT_BUF_SIZE];
static int s_rx_total, s_rx_have;

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        tk_set_flg(net_flg, NET_MQTT_UP);
        break;
    case MQTT_EVENT_DISCONNECTED:
        tk_clr_flg(net_flg, (UINT)~NET_MQTT_UP);
        break;
    case MQTT_EVENT_DATA:
        if (e->current_data_offset == 0) {
            int tl = e->topic_len < (int)sizeof s_rx_topic - 1 ? e->topic_len : (int)sizeof s_rx_topic - 1;
            memcpy(s_rx_topic, e->topic, (size_t)tl);
            s_rx_topic[tl] = 0;
            s_rx_total = e->total_data_len;
            s_rx_have = 0;
        }
        if (s_rx_have + e->data_len <= (int)sizeof s_rx_buf) {
            memcpy(s_rx_buf + s_rx_have, e->data, (size_t)e->data_len);
            s_rx_have += e->data_len;
        } else {
            s_rx_have = (int)sizeof s_rx_buf;   // keep the head; route_message truncates
        }
        if (e->current_data_offset + e->data_len >= s_rx_total) route_message(s_rx_topic, s_rx_buf, s_rx_have);
        break;
    default:
        break;
    }
}

// ---------- Wi-Fi (handlers run in IDF's default event-loop task) ----------

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        tk_clr_flg(net_flg, (UINT)~NET_WIFI_UP);
        uint32_t t = hal_time_ms();
        s_wifi_down_ms = t ? t : 1;
        int reason = d ? (int)d->reason : -1;
        if (reason != s_wifi_reason) { s_wifi_reason = reason; s_wifi_reason_new = true; }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_wifi_reason = -1;
        tk_set_flg(net_flg, NET_WIFI_UP);
    }
}

static const char *wifi_reason_text(int reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:           return "network not found (wrong name, or not 2.4 GHz)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:     return "authentication failed (wrong password?)";
    case WIFI_REASON_ASSOC_FAIL:            return "association refused";
    case WIFI_REASON_BEACON_TIMEOUT:        return "signal lost";
    default:                                return "see esp_wifi_types.h";
    }
}

// Initialises and starts the Wi-Fi driver (also used to restart it).
static bool wifi_driver_up(const char *ssid, const char *pass)
{
    esp_err_t err;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) { printf("[WIFI] esp_wifi_init failed: %d\n", (int)err); return false; }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // credentials come from the build; no flash write per connect
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    strncpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid - 1);
    strncpy((char *)wc.sta.password, pass, sizeof wc.sta.password - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;   // accept whatever the network offers, as Arduino did
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    if ((err = esp_wifi_start()) != ESP_OK) { printf("[WIFI] esp_wifi_start failed: %d\n", (int)err); return false; }
    esp_wifi_set_ps(WIFI_PS_NONE);            // always-on node: no modem sleep
    return true;
}

static bool wifi_start(const char *ssid, const char *pass)
{
    esp_err_t err = nvs_flash_init();       // the Wi-Fi driver keeps PHY calibration in NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) { printf("[WIFI] nvs_flash_init failed: %d\n", (int)err); return false; }
    if ((err = esp_netif_init()) != ESP_OK) { printf("[WIFI] esp_netif_init failed: %d\n", (int)err); return false; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { printf("[WIFI] event loop failed: %d\n", (int)err); return false; }
    if (!esp_netif_create_default_wifi_sta()) { printf("[WIFI] netif create failed\n"); return false; }
    // The driver's own info lines (association, block-ack, the access
    // point's MAC address) are off in the Arduino build; keep its warnings
    // and errors only, so the console output matches.
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, NULL);
    if (!wifi_driver_up(ssid, pass)) return false;
    s_wifi_started = true;
    return true;
}

// Console command WIFIDROP: disconnects Wi-Fi and holds it down for 10 s, so
// the reconnect path can be tested without touching the access point.
void mqtt_wifi_drop(void)
{
    if (!s_wifi_started) { printf("[WIFI] not started\n"); return; }
    printf("[WIFI] dropping the connection for 10 s (test)\n");
    s_wifi_hold_until = hal_time_ms() + 10000;
    esp_wifi_disconnect();
}

// Console command WIFISCAN: lists the networks the S3 can see.
void mqtt_wifi_scan(void)
{
    if (!s_wifi_started) { printf("[WIFI] not started (no credentials)\n"); return; }
    printf("[WIFI] scanning...\n");
    bool was_connected = net_flags() & NET_WIFI_UP;
    wifi_scan_config_t sc = { 0 };
    uint64_t t0 = hal_time_us();
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    uint32_t scan_ms = (uint32_t)((hal_time_us() - t0) / 1000);
    uint16_t n = 0;
    if (err == ESP_OK) esp_wifi_scan_get_ap_num(&n);
    static wifi_ap_record_t recs[20];
    uint16_t got = n < 20 ? n : 20;
    if (err == ESP_OK) esp_wifi_scan_get_ap_records(&got, recs);
    static const char *const auth[] = { "open", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2-ENT",
                                         "WPA3", "WPA2/WPA3", "WAPI", "OWE", "WPA3-ENT" };
    for (int i = 0; i < got; i++) {
        int a = (int)recs[i].authmode;
        printf("[WIFI] \"%s\" (%u chars) ch %2d rssi %4d %s %02x:%02x:%02x:%02x:%02x:%02x\n", (char *)recs[i].ssid,
               (unsigned)strlen((char *)recs[i].ssid), recs[i].primary, recs[i].rssi,
               a < (int)(sizeof auth / sizeof auth[0]) ? auth[a] : "other",
               recs[i].bssid[0], recs[i].bssid[1], recs[i].bssid[2], recs[i].bssid[3], recs[i].bssid[4], recs[i].bssid[5]);
    }
    printf("[WIFI] %u networks; this node uses \"%s\", status %s (scan err %d, %u ms)\n", (unsigned)n,
           runtime_wifi_ssid(), was_connected ? "connected" : "not connected", (int)err, (unsigned)scan_ms);
}

// ---------- MQTTTask ----------

static void on_connected(void)
{
    printf("[MQTT] connected to broker\n");
    char topic[PUB_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/#", s_node);
    printf("[MQTT] Subscribing to node wildcard: %s\n", topic);
    printf(esp_mqtt_client_subscribe(s_client, topic, 1) >= 0 ? "[MQTT] Subscribed OK to: node-wildcard\n"
                                                              : "[MQTT] FAILED to subscribe to: node-wildcard\n");
    printf("[MQTT] Subscribing to broadcast: pico/broadcast/config\n");
    printf(esp_mqtt_client_subscribe(s_client, "pico/broadcast/config", 1) >= 0 ? "[MQTT] Subscribed OK to: broadcast\n"
                                                                               : "[MQTT] FAILED to subscribe to: broadcast\n");
    int nf = __atomic_load_n(&s_nfollows, __ATOMIC_ACQUIRE);
    for (int i = 0; i < nf; i++) esp_mqtt_client_subscribe(s_client, s_follows[i], 1);

    char payload[128];
    snprintf(topic, sizeof topic, "pico/%s/presence", s_node);
    snprintf(payload, sizeof payload, "{\"node\":\"%s\",\"cap\":[\"config\",\"telemetry\",\"node-msg\"]}", s_node);
    esp_mqtt_client_publish(s_client, topic, payload, (int)strlen(payload), 1, 1);
}

static bool mqtt_start_client(void)
{
    static char uri[96];
    snprintf(uri, sizeof uri, "mqtt://%s:%u", s_host, (unsigned)s_port);
    esp_mqtt_client_config_t cfg = { 0 };
    cfg.broker.address.uri = uri;
    cfg.credentials.client_id = s_client_id;
    cfg.session.keepalive = 30;
    cfg.session.disable_clean_session = false;
    cfg.network.reconnect_timeout_ms = 5000;
    cfg.network.timeout_ms = 2000;
    cfg.buffer.size = MQTT_BUF_SIZE;
    cfg.buffer.out_size = 1024;
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) { printf("[MQTT] client init failed\n"); return false; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    printf("[MQTT] Connecting to %s port %u...\n", s_host, (unsigned)s_port);
    if (esp_mqtt_client_start(s_client) != ESP_OK) { printf("[MQTT] client start failed\n"); return false; }
    return true;
}

void mqtt_task_body(void)
{
    static pub_msg_t m;
    const char *ssid = runtime_wifi_ssid();

    if (!ssid[0]) {
        printf("[WIFI] no credentials; running offline\n");
        for (;;) {                           // nothing to publish to
            tk_rcv_mbf(pub_mbf, &m, 1000);
            runtime_alive(RT_MQTT);
        }
    }

    printf("[WIFI] Connecting to %s\n", ssid);
    if (!wifi_start(ssid, runtime_wifi_pass())) printf("WiFi connect failed\n");

    bool wifi_up = false, mqtt_up = false;
    uint32_t next_status = 0, next_heartbeat = 0, lost_since = 0;

    for (;;) {
        runtime_alive(RT_MQTT);
        uint32_t now = hal_time_ms();
        UINT flags = net_flags();

        if (s_wifi_reason_new) {             // report each new cause once, not every retry
            s_wifi_reason_new = false;
            int r = s_wifi_reason;
            if (r >= 0) printf("[WIFI] not connected: reason %d, %s\n", r, wifi_reason_text(r));
        }
        if (s_wifi_hold_until && (int32_t)(now - s_wifi_hold_until) >= 0) {
            s_wifi_hold_until = 0;
            s_wifi_down_ms = 0;
            printf("[WIFI] hold over, reconnecting\n");
            esp_wifi_connect();
        }
        bool wifi_now = flags & NET_WIFI_UP;
        if (wifi_now && !wifi_up) {
            printf("WiFi connected\n[WIFI] ip=%s\n", s_ip);
        } else if (!wifi_now && wifi_up) {
            printf("[WIFI] connection lost; reconnecting\n");
        }
        wifi_up = wifi_now;
        // The driver does not retry by itself: 2 s after each disconnect event
        // (not during an attempt, and not while WIFIDROP holds the link down)
        // ask it to connect again.
        uint32_t down = s_wifi_down_ms;
        if (!wifi_up && down && !s_wifi_hold_until && now - down >= 2000) {
            s_wifi_down_ms = 0;
            esp_wifi_connect();
        }
        // On the course port's SMP profile the radio can stop receiving for a
        // while: scans find no access point and reconnects fail with "network
        // not found" (MTK_REDESIGN.md section 10). If the link has not come
        // back within 15 s, restart the driver (stop, deinit, init, start)
        // and connect again; this repeats every 15 s until it connects.
        if (wifi_up || s_wifi_hold_until) {
            lost_since = 0;
        } else if (s_wifi_started) {
            if (!lost_since) lost_since = now;
            if (now - lost_since >= 15000) {
                printf("[WIFI] no reconnect for 15 s; restarting the Wi-Fi driver\n");
                s_wifi_down_ms = 0;
                esp_wifi_stop();
                esp_wifi_deinit();
                wifi_driver_up(ssid, runtime_wifi_pass());   // STA_START connects again
                lost_since = now;
            }
        }

        if (wifi_up && s_configured && !s_client) mqtt_start_client();

        bool mqtt_now = flags & NET_MQTT_UP;
        if (mqtt_now && !mqtt_up) on_connected();
        else if (!mqtt_now && mqtt_up) printf("[MQTT] disconnected from broker\n");
        mqtt_up = mqtt_now;

        if ((int32_t)(now - next_status) >= 0) {
            printf("[MQTT] Status: %s, Broker: %s\n", mqtt_up ? "Connected" : "Disconnected", s_host);
            next_status = now + 5000;
        }
        if (mqtt_up && (int32_t)(now - next_heartbeat) >= 0) {
            char topic[PUB_TOPIC_MAX], payload[48];
            snprintf(topic, sizeof topic, "pico/%s/heartbeat", s_node);
            snprintf(payload, sizeof payload, "{\"uptime_s\":%u}", (unsigned)(now / 1000));
            esp_mqtt_client_publish(s_client, topic, payload, (int)strlen(payload), 0, 0);
            next_heartbeat = now + 10000;
        }

        // Drain pub_mbf, waiting at most 10 ms so the status and Wi-Fi checks keep running.
        INT got = tk_rcv_mbf(pub_mbf, &m, 10);
        while (got > 0) {
            if (mqtt_up) {                   // offline: dropped, as on the Pico
                if (m.op == PUB_SUBSCRIBE) esp_mqtt_client_subscribe(s_client, m.topic, m.qos);
                else if (esp_mqtt_client_publish(s_client, m.topic, m.payload, m.len, m.qos, m.retain) < 0)
                    printf("[MQTT] pub err on %s\n", m.topic);
            }
            got = tk_rcv_mbf(pub_mbf, &m, TMO_POL);
        }
    }
}
