#pragma once
#include <Arduino.h>

// MQTT over TLS (HiveMQ Cloud, port 8883).
// Every topic lives under "clock/<deviceId>/". Functions below take the part
// AFTER that prefix (e.g. "cmd/msg", "config").
namespace mqtt_link {

// topicSuffix: e.g. "cmd/msg"; payload is NUL-terminated.
using Handler = void (*)(const char* topicSuffix, const char* payload, size_t len);

// onConnect: after every successful connection; onFail: after every failed attempt.
void begin(const char* deviceId, Handler onMessage, void (*onConnect)(), void (*onFail)(int state));
void loop(bool networkReady);  // networkReady = Wi-Fi up and clock set (TLS needs the date)
bool connected();
const char* baseTopic();       // "clock/<deviceId>"

bool publish(const char* topicSuffix, const char* payload, bool retained = false);

}  // namespace mqtt_link
