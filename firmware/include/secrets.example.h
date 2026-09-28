#pragma once
// Modelo de credenciais — usado a partir da etapa 2 (MQTT).
// Copie para secrets.h (que NÃO vai para o git) e preencha:
//   cp include/secrets.example.h include/secrets.h

#define MQTT_HOST "4b9a673f16e84df093793b8d8768d7f6.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_USER "esp32c3sm_clock"
#define MQTT_PASS "coloque-a-senha-aqui"
#define DEVICE_ID "sala"   // identifica este relógio nos tópicos: clock/<DEVICE_ID>/...
