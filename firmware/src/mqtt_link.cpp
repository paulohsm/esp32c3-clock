#include "mqtt_link.h"
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include "root_ca.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h — copy include/secrets.example.h to include/secrets.h and fill it in."
#endif

namespace mqtt_link {

static WiFiClientSecure net;
static PubSubClient client(net);
static char base[48];
static char clientId[32];
static Handler handler = nullptr;
static void (*connectCb)() = nullptr;

static uint32_t lastAttempt = 0;
static uint32_t retryDelay = 2000;           // grows up to 60 s after failures
static constexpr uint32_t RETRY_MAX = 60000;

static void onRaw(char* topic, byte* payload, unsigned int len) {
  size_t baseLen = strlen(base);
  if (strncmp(topic, base, baseLen) != 0 || topic[baseLen] != '/') return;
  // Copy topic and payload: PubSubClient reuses its buffer when we publish
  // from inside the handler (e.g. the "ack" reply).
  static char suffix[64];
  static char buf[1024];
  strlcpy(suffix, topic + baseLen + 1, sizeof(suffix));
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';
  if (handler) handler(suffix, buf, n);
}

void begin(const char* deviceId, Handler onMessage, void (*onConnect)()) {
  snprintf(base, sizeof(base), "clock/%s", deviceId);
  strlcpy(clientId, deviceId, sizeof(clientId));
  handler = onMessage;
  connectCb = onConnect;

  net.setCACert(ROOT_CA_PEM);
  net.setHandshakeTimeout(10);
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setBufferSize(2048);   // JSON with the schedule list can be large
  client.setKeepAlive(30);
  client.setSocketTimeout(10);
  client.setCallback(onRaw);
}

const char* baseTopic() { return base; }

bool connected() { return client.connected(); }

static bool tryConnect() {
  char willTopic[64];
  snprintf(willTopic, sizeof(willTopic), "%s/online", base);
  Serial.printf("MQTT: connecting to %s:%d as %s ...\n", MQTT_HOST, MQTT_PORT, MQTT_USER);

  // Last Will: if the clock drops off, the broker publishes "0" (retained) for us.
  bool ok = client.connect(clientId, MQTT_USER, MQTT_PASS, willTopic, 1, true, "0");
  if (!ok) {
    // -4 timeout, -2 network/TLS failure, 4 bad user/password, 5 not authorized
    Serial.printf("MQTT: failed, state=%d\n", client.state());
    return false;
  }
  Serial.println("MQTT: connected");
  client.publish(willTopic, "1", true);

  char sub[64];
  snprintf(sub, sizeof(sub), "%s/cmd/#", base);
  client.subscribe(sub, 1);
  snprintf(sub, sizeof(sub), "%s/config/set", base);
  client.subscribe(sub, 1);

  if (connectCb) connectCb();
  return true;
}

void loop(bool networkReady) {
  if (client.connected()) {
    client.loop();
    return;
  }
  if (!networkReady) return;
  if (millis() - lastAttempt < retryDelay) return;
  lastAttempt = millis();
  if (tryConnect()) {
    retryDelay = 2000;
  } else {
    retryDelay = min(retryDelay * 2, RETRY_MAX);
  }
}

bool publish(const char* suffix, const char* payload, bool retained) {
  if (!client.connected()) return false;
  char topic[96];
  snprintf(topic, sizeof(topic), "%s/%s", base, suffix);
  return client.publish(topic, payload, retained);
}

}  // namespace mqtt_link
