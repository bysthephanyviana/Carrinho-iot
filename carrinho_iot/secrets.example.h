// Copie este arquivo para secrets.h (na mesma pasta) e preencha.
// secrets.h esta no .gitignore: NAO suba senhas para o GitHub.

#pragma once

// Rede Wi-Fi 2,4 GHz (o ESP32 nao conecta em 5 GHz)
#define WIFI_SSID "nome-da-rede"
#define WIFI_SENHA "senha-da-rede"

// ThingSpeak > seu canal > API Keys > Write API Key
#define THINGSPEAK_API_KEY "XXXXXXXXXXXXXXXX"

// Blynk > Devices > seu dispositivo > Device Info
// (copie as 3 linhas BLYNK_ que aparecem la)
#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Carrinho IoT"
#define BLYNK_AUTH_TOKEN "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
// Servidor da sua regiao, mostrado em Device Info (ex.: ny3.blynk.cloud).
// "blynk.cloud" funciona na maioria dos casos.
#define BLYNK_SERVIDOR "blynk.cloud"
