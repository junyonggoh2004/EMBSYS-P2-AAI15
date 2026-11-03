// mqtt_s3.cpp
// Wi-Fi and MQTT for the ESP32-S3 (D1 task 19): MQTTTask and the core's
// mqtt_telemetry.h interface. Library: MQTT by Joel Gaehwiler, 2.5.3.
//
// Ownership (Week 6 design, section 3): MQTTTask on core 0 owns Wi-Fi and the
// MQTT client. Inbound configuration, commands and bridged sensor lines are
// copied into sched_q, so only SchedulerTask applies them. Everything the core
// publishes, and every subscription it asks for, goes through pub_q.
//
// Topics and payloads match the Pico reference (project_v3/mqtt/mqtt_telemetry.c)
// with three intended fixes: remote sensor lines are bridged (D-10), the boot
// log reports the real Wi-Fi state (D-09), and uptime_s is in seconds (D-12).
//
// Limits: an inbound payload longer than SCHED_TEXT_MAX - 1 bytes is truncated
// with a warning; when sched_q is full the new message is dropped and logged;
// when the broker is unreachable, publishing returns false and nothing is
// queued, as on the Pico. Reconnection is tried every 5 s.
#include <Arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <stdarg.h>
#include <string.h>

#include <EdgeCore.h>
#include "mqtt/mqtt_telemetry.h"
#include "mqtt/sensor_bridge.h"
extern "C" {
#include "mqtt/mqtt_model_transfer.h"
}
#include "runtime_s3.h"

static char s_ssid[33], s_pass[65], s_host[64];
static uint16_t s_port = 1883;
static char s_node[64] = "pico-node";
static char s_client_id[64] = "pico";
static volatile bool s_configured;     // set by mqtt_init() in SchedulerTask

#define MAX_FOLLOWS 8
static char s_follows[MAX_FOLLOWS][PUB_TOPIC_MAX];   // re-subscribed on every connect
static int s_nfollows;

static WiFiClient s_net;
static MQTTClient s_client(SCHED_TEXT_MAX + 256);

extern "C" void runtime_set_network(const char *ssid, const char *pass, const char *mqtt_host,
                                    uint16_t mqtt_port, const char *node_id) {
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid ? ssid : "");
    snprintf(s_pass, sizeof s_pass, "%s", pass ? pass : "");
    snprintf(s_host, sizeof s_host, "%s", mqtt_host ? mqtt_host : "");
    s_port = mqtt_port ? mqtt_port : 1883;
    snprintf(s_node, sizeof s_node, "%s", node_id ? node_id : "pico-node");
}

extern "C" const char *runtime_node_id(void) { return s_node; }
extern "C" const char *runtime_broker_host(void) { return s_host; }
extern "C" uint16_t runtime_broker_port(void) { return s_port; }

// ---------- the core's interface (called from SchedulerTask) ----------

extern "C" bool mqtt_is_connected(void) {
    return net_events && (xEventGroupGetBits(net_events) & NET_MQTT_UP);
}

static bool enqueue(uint8_t op, const char *topic, const char *payload, size_t len, int qos, bool retain) {
    if (!pub_q || (op == PUB_PUBLISH && !mqtt_is_connected())) return false;
    static pub_msg_t m;                 // callers all run in SchedulerTask
    m.op = op;
    snprintf(m.topic, sizeof m.topic, "%s", topic);
    if (len >= sizeof m.payload) len = sizeof m.payload - 1;
    memcpy(m.payload, payload, len);
    m.payload[len] = 0;
    m.len = (uint16_t)len;
    m.qos = (uint8_t)qos;
    m.retain = retain;
    if (xQueueSend(pub_q, &m, 0) != pdTRUE) {
        static uint32_t s_last_warn;
        if (hal_time_ms() - s_last_warn >= 1000) {
            printf("[MQTT] publish queue full, dropped %s\n", topic);
            s_last_warn = hal_time_ms();
        }
        return false;
    }
    return true;
}

extern "C" bool mqtt_init(const char *broker_host, uint16_t broker_port, const char *client_id,
                          mqtt_cfg_block_cb_t cfg_cb, mqtt_cmd_cb_t cmd_cb, mqtt_node_msg_cb_t node_cb) {
    runtime_set_mqtt_handlers(cfg_cb, cmd_cb, node_cb);
    if (broker_host && *broker_host) snprintf(s_host, sizeof s_host, "%s", broker_host);
    if (broker_port) s_port = broker_port;
    snprintf(s_client_id, sizeof s_client_id, "%s", (client_id && *client_id) ? client_id : "pico");
    s_configured = s_host[0] != 0;
    return s_configured;
}

extern "C" void mqtt_set_node_id(const char *node_id) {
    snprintf(s_node, sizeof s_node, "%s", (node_id && *node_id) ? node_id : "pico-node");
}

extern "C" void mqtt_poll(void) {}     // MQTTTask does the work

extern "C" bool mqtt_pub_text(const char *topic, const char *fmt, ...) {
    char payload[PUB_PAYLOAD_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(payload, sizeof payload, fmt, ap);
    va_end(ap);
    return enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

extern "C" bool mqtt_pub_bytes(const char *sensor_name, const uint8_t *bytes, size_t len, int qos, bool retain) {
    (void)sensor_name; (void)bytes; (void)len; (void)qos; (void)retain;
    return false;    // the /raw topic is disabled in the reference too
}

extern "C" bool mqtt_pub_line(const char *sensor_name, const char *line, int qos, bool retain) {
    if (!sensor_name || !*sensor_name || !line) return false;
    char topic[PUB_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/sensor/%s/line", s_node, sensor_name);
    return enqueue(PUB_PUBLISH, topic, line, strlen(line), qos, retain);
}

extern "C" bool mqtt_pub_event(const char *rule_name, const char *event_type, double value,
                               const char *msg, uint32_t ts_ms) {
    if (!rule_name || !*rule_name) return false;
    char topic[PUB_TOPIC_MAX], payload[PUB_PAYLOAD_MAX];
    snprintf(topic, sizeof topic, "pico/rule/%s/event", rule_name);
    snprintf(payload, sizeof payload, "ts=%u rule=%s type=%s value=%.3f msg=%s", (unsigned)ts_ms,
             rule_name, event_type ? event_type : "log", value, msg ? msg : "");
    return enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

static bool pub_status(const char *kind, const char *name, bool ok) {
    if (!name || !*name) return false;
    char topic[PUB_TOPIC_MAX], payload[64];
    snprintf(topic, sizeof topic, "pico/%s/status/%s", s_node, kind);
    snprintf(payload, sizeof payload, "%s:%s", ok ? "OK" : "FAIL", name);
    return enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

extern "C" bool mqtt_pub_cfg_status(const char *name, bool ok) { return pub_status("config", name, ok); }
extern "C" bool mqtt_pub_rule_status(const char *name, bool ok) { return pub_status("rule", name, ok); }

extern "C" void mqtt_publish_birth(bool retained) {
    char topic[PUB_TOPIC_MAX], payload[128];
    snprintf(topic, sizeof topic, "pico/%s/presence", s_node);
    snprintf(payload, sizeof payload, "{\"node\":\"%s\",\"cap\":[\"config\",\"telemetry\",\"node-msg\"]}", s_node);
    enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 1, retained);
}

extern "C" void mqtt_publish_heartbeat(uint32_t uptime_s) {
    char topic[PUB_TOPIC_MAX], payload[48];
    snprintf(topic, sizeof topic, "pico/%s/heartbeat", s_node);
    snprintf(payload, sizeof payload, "{\"uptime_s\":%u}", (unsigned)uptime_s);
    enqueue(PUB_PUBLISH, topic, payload, strlen(payload), 0, false);
}

extern "C" bool mqtt_follow_remote_source(const char *other_node_id, const char *remote_source,
                                          const char *local_alias) {
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
    if (!known && s_nfollows < MAX_FOLLOWS) snprintf(s_follows[s_nfollows++], PUB_TOPIC_MAX, "%s", topic);
    enqueue(PUB_SUBSCRIBE, topic, "", 0, 1, false);   // also subscribed again on every connect
    printf("[MQTT] follow_remote_source: %s/%s -> %s via %s\n", other_node_id, remote_source, local_alias, topic);
    return true;
}

// ---------- inbound routing (runs in MQTTTask, inside client.loop()) ----------

static void route_message(MQTTClient *client, char topic[], char bytes[], int length) {
    (void)client;
    static sched_msg_t msg;
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
        }
        else if (!strncmp(rest, "model/", 6)) {
            // binary chunks go straight to the transfer code, as on the Pico
            printf("[MQTT] Model data on: %s (%d bytes)\n", topic, length);
            model_transfer_on_data(topic, (const uint8_t *)bytes, (size_t)length);
            return;
        }
        // own status, sensor, presence and heartbeat echoes are dropped
    } else if (!strcmp(topic, "pico/broadcast/config")) {
        msg.kind = SCHED_MQTT_CONFIG;
    } else if (strstr(topic, "/sensor/") && strlen(topic) > 5 &&
               !strcmp(topic + strlen(topic) - 5, "/line")) {
        msg.kind = SCHED_REMOTE_LINE;     // a followed node's sensor (D-10)
    }
    if (!msg.kind) return;

    if (length > SCHED_TEXT_MAX - 1) {
        printf("[MQTT] payload of %d bytes on %s truncated to %d\n", length, topic, SCHED_TEXT_MAX - 1);
        length = SCHED_TEXT_MAX - 1;
    }
    snprintf(msg.topic, sizeof msg.topic, "%s", topic);
    memcpy(msg.text, bytes, length);
    msg.text[length] = 0;
    msg.len = (uint16_t)length;
    if (xQueueSend(sched_q, &msg, 0) != pdTRUE)
        printf("[MQTT] inbound queue full, dropped message on %s\n", topic);
}

// ---------- Wi-Fi diagnostics ----------

static const char *wifi_reason_text(int reason) {
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

static void on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
    static int s_last_reason = -1;
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        int reason = info.wifi_sta_disconnected.reason;
        if (reason != s_last_reason)   // report each new cause once, not every retry
            printf("[WIFI] not connected: reason %d, %s\n", reason, wifi_reason_text(reason));
        s_last_reason = reason;
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        s_last_reason = -1;
    }
}

// Console command WIFIDROP: disconnects Wi-Fi and holds it down for 10 s, so
// the reconnect path can be tested without touching the access point.
static volatile uint32_t s_wifi_hold_until;

extern "C" void mqtt_wifi_drop(void) {
    printf("[WIFI] dropping the connection for 10 s (test)\n");
    s_wifi_hold_until = hal_time_ms() + 10000;
    WiFi.disconnect();
}

// Console command WIFISCAN: lists the networks the S3 can see.
extern "C" void mqtt_wifi_scan(void) {
    printf("[WIFI] scanning...\n");
    bool was_connected = WiFi.status() == WL_CONNECTED;
    if (!was_connected) WiFi.disconnect();   // a scan fails while a connection attempt runs
    int n = WiFi.scanNetworks();
    static const char *const auth[] = { "open", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2-ENT",
                                         "WPA3", "WPA2/WPA3", "WAPI", "OWE", "WPA3-ENT" };
    for (int i = 0; i < n; i++) {
        int a = (int)WiFi.encryptionType(i);
        printf("[WIFI] \"%s\" (%u chars) ch %2d rssi %4d %s %s\n", WiFi.SSID(i).c_str(),
               (unsigned)WiFi.SSID(i).length(), WiFi.channel(i), WiFi.RSSI(i),
               a < (int)(sizeof auth / sizeof auth[0]) ? auth[a] : "other", WiFi.BSSIDstr(i).c_str());
    }
    printf("[WIFI] %d networks; this node uses \"%s\", status %s\n", n < 0 ? 0 : n, s_ssid,
           WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
    WiFi.scanDelete();
    if (!was_connected) WiFi.begin(s_ssid, s_pass);
}

// ---------- MQTTTask ----------

static void on_connected(void) {
    printf("[MQTT] connected to broker\n");
    char topic[PUB_TOPIC_MAX];
    snprintf(topic, sizeof topic, "pico/%s/#", s_node);
    printf("[MQTT] Subscribing to node wildcard: %s\n", topic);
    printf(s_client.subscribe(topic, 1) ? "[MQTT] Subscribed OK to: node-wildcard\n"
                                        : "[MQTT] FAILED to subscribe to: node-wildcard\n");
    printf("[MQTT] Subscribing to broadcast: pico/broadcast/config\n");
    printf(s_client.subscribe("pico/broadcast/config", 1) ? "[MQTT] Subscribed OK to: broadcast\n"
                                                          : "[MQTT] FAILED to subscribe to: broadcast\n");
    for (int i = 0; i < s_nfollows; i++) s_client.subscribe(s_follows[i], 1);

    char payload[128];
    snprintf(topic, sizeof topic, "pico/%s/presence", s_node);
    snprintf(payload, sizeof payload, "{\"node\":\"%s\",\"cap\":[\"config\",\"telemetry\",\"node-msg\"]}", s_node);
    s_client.publish(topic, payload, (int)strlen(payload), true, 1);
}

extern "C" void mqtt_task_body(void) {
    static pub_msg_t m;
    bool wifi_up = false, mqtt_up = false;
    uint32_t next_try = 0, next_status = 0, next_heartbeat = 0;

    if (!s_ssid[0]) {
        printf("[WIFI] no credentials; running offline\n");
        for (;;) xQueueReceive(pub_q, &m, portMAX_DELAY);   // nothing to publish to
    }

    WiFi.onEvent(on_wifi_event);
    WiFi.persistent(false);       // credentials come from the sketch; no flash write on every connect
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);         // always-on node: no modem sleep (one boot in six froze CPU1 without this)
    WiFi.setAutoReconnect(true);
    printf("[WIFI] Connecting to %s\n", s_ssid);
    WiFi.begin(s_ssid, s_pass);
    s_client.begin(s_host, s_port, s_net);
    s_client.setKeepAlive(30);
    s_client.setCleanSession(true);
    s_client.setTimeout(2000);
    s_client.onMessageAdvanced(route_message);

    for (;;) {
        uint32_t now = hal_time_ms();

        if (s_wifi_hold_until && (int32_t)(now - s_wifi_hold_until) >= 0) {
            s_wifi_hold_until = 0;
            printf("[WIFI] hold over, reconnecting\n");
            WiFi.begin(s_ssid, s_pass);
        }
        if (s_wifi_hold_until && WiFi.status() == WL_CONNECTED) WiFi.disconnect();

        bool wifi_now = WiFi.status() == WL_CONNECTED;
        if (wifi_now && !wifi_up) {
            printf("WiFi connected\n[WIFI] ip=%s\n", WiFi.localIP().toString().c_str());
            xEventGroupSetBits(net_events, NET_WIFI_UP);
        } else if (!wifi_now && wifi_up) {
            printf("[WIFI] connection lost; reconnecting\n");
            xEventGroupClearBits(net_events, NET_WIFI_UP);
        }
        wifi_up = wifi_now;

        if (wifi_up && s_configured && !s_client.connected() && (int32_t)(now - next_try) >= 0) {
            next_try = now + 5000;
            printf("[MQTT] Connecting to %s port %u...\n", s_host, (unsigned)s_port);
            if (s_client.connect(s_client_id)) on_connected();
            else printf("[MQTT] connect failed (lwmqtt err %d, rc %d)\n", (int)s_client.lastError(), (int)s_client.returnCode());
        }

        bool mqtt_now = s_client.connected();
        if (mqtt_now != mqtt_up) {
            if (mqtt_now) xEventGroupSetBits(net_events, NET_MQTT_UP);
            else { xEventGroupClearBits(net_events, NET_MQTT_UP); printf("[MQTT] disconnected from broker\n"); }
            mqtt_up = mqtt_now;
        }
        if (mqtt_up) s_client.loop();

        if ((int32_t)(now - next_status) >= 0) {
            printf("[MQTT] Status: %s, Broker: %s\n", mqtt_up ? "Connected" : "Disconnected", s_host);
            next_status = now + 5000;
        }
        if (mqtt_up && (int32_t)(now - next_heartbeat) >= 0) {
            char topic[PUB_TOPIC_MAX], payload[48];
            snprintf(topic, sizeof topic, "pico/%s/heartbeat", s_node);
            snprintf(payload, sizeof payload, "{\"uptime_s\":%u}", (unsigned)(now / 1000));
            s_client.publish(topic, payload, (int)strlen(payload), false, 0);
            next_heartbeat = now + 10000;
        }

        // Drain pub_q, waiting at most 10 ms so the client loop keeps running.
        if (xQueueReceive(pub_q, &m, pdMS_TO_TICKS(10)) == pdTRUE) {
            do {
                if (!mqtt_up) continue;   // offline: dropped, as on the Pico
                if (m.op == PUB_SUBSCRIBE) s_client.subscribe(m.topic, m.qos);
                else if (!s_client.publish(m.topic, m.payload, m.len, m.retain, m.qos))
                    printf("[MQTT] pub err on %s\n", m.topic);
            } while (xQueueReceive(pub_q, &m, 0) == pdTRUE);
        }
    }
}
