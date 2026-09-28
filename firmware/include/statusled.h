#pragma once
#include <Arduino.h>

// LED azul da placa (GPIO 8) como indicador de estado.
//   FAST  = conectando ao Wi-Fi
//   SLOW  = Wi-Fi ok, aguardando serviço (NTP agora; MQTT na etapa 2)
//   OFF   = tudo conectado
//   SOLID = portal de configuração de Wi-Fi aberto
//   flash() = piscada curta: comando recebido (só quando está OFF)
namespace statusled {

enum Mode : uint8_t { OFF, FAST, SLOW, SOLID };

void begin();
void set(Mode mode);
void flash();

}  // namespace statusled
