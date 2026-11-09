#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "secrets.h"
#include <BlynkSimpleEsp32.h>

const uint8_t TRIG_ESQ = 16;
const uint8_t ECHO_ESQ = 17;
const uint8_t TRIG_DIR = 18;
const uint8_t ECHO_DIR = 19;
const uint8_t TRIG_FRENTE = 21;
const uint8_t ECHO_FRENTE = 22;

const uint8_t ENA = 25;
const uint8_t IN1 = 26;
const uint8_t IN2 = 27;
const uint8_t ENB = 33;
const uint8_t IN3 = 32;
const uint8_t IN4 = 23;

const uint8_t VELOCIDADE = 180;
const uint8_t VELOCIDADE_GIRO = 150;

const int LIMITE_CAMINHO_LIVRE = 55;
const int LIMITE_OBSTACULO = 50;
const int LIMITE_LATERAL_PERTO = 20;
const int DIST_MUITO_PERTO = 15;
const int DECAIMENTO_SEM_LEITURA = 15;
const int DIST_MAXIMA = 100;

const uint32_t TEMPO_GIRO_MS = 250;
const uint32_t TEMPO_RE_MS = 400;
const uint32_t TIMEOUT_ECHO_US = 20000;
const uint32_t INTERVALO_SENSORES_MS = 60;
const uint32_t ESPERA_MODO_BLYNK_MS = 20000;
const uint32_t TIMEOUT_CONEXAO_WIFI_MS = 20000;
const uint32_t INTERVALO_THINGSPEAK_MS = 20000;
const uint32_t INTERVALO_BLYNK_MS = 2000;

const uint8_t MOV_PARADO = 0;
const uint8_t MOV_FRENTE = 1;
const uint8_t MOV_ESQUERDA = 2;
const uint8_t MOV_DIREITA = 3;
const uint8_t MOV_TRAS = 4;
const char *const NOMES_MOVIMENTO[] = {"PARADO", "FRENTE", "ESQUERDA", "DIREITA", "TRAS"};

#define VP_FRENTE      V0
#define VP_DIREITA     V1
#define VP_ESQUERDA    V2
#define VP_MOVIMENTO   V3
#define VP_DESVIOS     V4
#define VP_MODO        V5
#define VP_BT_FRENTE   V6
#define VP_BT_TRAS     V7
#define VP_BT_ESQ      V8
#define VP_BT_DIR      V9
#define VP_DESVIOS_ESQ V10
#define VP_DESVIOS_DIR V11

const char *const OTA_NOME = "carrinho-iot";

struct RedeWiFi {
  const char *nome;
  const char *senha;
};
#ifdef WIFI_REDES
const RedeWiFi REDES_WIFI[] = WIFI_REDES;
#else
const RedeWiFi REDES_WIFI[] = {{WIFI_SSID, WIFI_SENHA}};
#endif

struct DadosNuvem {
  int frente;
  int direita;
  int esquerda;
  uint8_t movimento;
  uint32_t desvios;
  bool manual;
  uint32_t desviosEsquerda;
  uint32_t desviosDireita;
};

uint8_t codigoMovimento = MOV_PARADO;
volatile bool modoManual = false;
volatile bool modoSincronizado = false;
volatile uint8_t comandoManual = MOV_PARADO;
volatile bool blynkConectado = false;
volatile bool atualizandoOTA = false;

int ultimaFrenteValida = DIST_MAXIMA;
bool memoriaFrente = false;

uint32_t totalDesvios = 0;
uint32_t desviosEsquerda = 0;
uint32_t desviosDireita = 0;
bool estavaDesviando = false;

Preferences preferencias;
DadosNuvem dadosNuvem = {0, 0, 0, MOV_PARADO, 0, false, 0, 0};
portMUX_TYPE muxNuvem = portMUX_INITIALIZER_UNLOCKED;

void definirMotores(uint8_t velocidade, bool in1, bool in2, bool in3, bool in4, uint8_t movimento) {
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
  digitalWrite(IN1, in1);
  digitalWrite(IN2, in2);
  digitalWrite(IN3, in3);
  digitalWrite(IN4, in4);
  codigoMovimento = movimento;
}

void carroFrente()   { definirMotores(VELOCIDADE, LOW, HIGH, LOW, HIGH, MOV_FRENTE); }
void carroTras()     { definirMotores(VELOCIDADE, HIGH, LOW, HIGH, LOW, MOV_TRAS); }
void carroEsquerda() { definirMotores(VELOCIDADE_GIRO, HIGH, LOW, LOW, HIGH, MOV_ESQUERDA); }
void carroDireita()  { definirMotores(VELOCIDADE_GIRO, LOW, HIGH, HIGH, LOW, MOV_DIREITA); }

void paraCarro() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  codigoMovimento = MOV_PARADO;
}

int lerDistancia(uint8_t trig, uint8_t echo, bool &semLeitura) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  const uint32_t duracao = pulseIn(echo, HIGH, TIMEOUT_ECHO_US);
  const int distancia = duracao / 58;
  semLeitura = duracao == 0 || distancia <= 0;
  if (semLeitura) {
    return DIST_MAXIMA;
  }
  return min(distancia, DIST_MAXIMA);
}

void carregarDesvios() {
  preferencias.begin("carrinho", false);
  totalDesvios = preferencias.getULong("total", 0);
  desviosEsquerda = preferencias.getULong("esq", 0);
  desviosDireita = preferencias.getULong("dir", 0);
}

void salvarDesvios() {
  preferencias.putULong("total", totalDesvios);
  preferencias.putULong("esq", desviosEsquerda);
  preferencias.putULong("dir", desviosDireita);
}

DadosNuvem lerDadosNuvem() {
  portENTER_CRITICAL(&muxNuvem);
  const DadosNuvem dados = dadosNuvem;
  portEXIT_CRITICAL(&muxNuvem);
  return dados;
}

void enviarThingSpeak(const DadosNuvem &dados) {
  String url = "http://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_API_KEY;
  url += "&field1=" + String(dados.frente);
  url += "&field2=" + String(dados.direita);
  url += "&field3=" + String(dados.esquerda);
  url += "&field4=" + String(dados.movimento);
  url += "&field5=" + String(dados.desvios);
  url += "&field6=" + String(dados.manual ? 1 : 0);
  url += "&field7=" + String(dados.desviosEsquerda);
  url += "&field8=" + String(dados.desviosDireita);

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(url);
  const int codigoHttp = http.GET();
  const String resposta = codigoHttp > 0 ? http.getString() : "";
  http.end();

  if (codigoHttp == 200 && resposta != "0") {
    Serial.println("ThingSpeak: enviado, registro " + resposta);
  } else {
    Serial.println("ThingSpeak: falha no envio (HTTP " + String(codigoHttp) + ")");
  }
}

void tarefaThingSpeak(void *) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    enviarThingSpeak(lerDadosNuvem());
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_THINGSPEAK_MS));
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(VP_MODO);
}

BLYNK_WRITE(VP_MODO) {
  modoManual = param.asInt() == 1;
  modoSincronizado = true;
  comandoManual = MOV_PARADO;
  Serial.println(modoManual ? "Blynk: modo MANUAL" : "Blynk: modo AUTOMATICO");
}

void tratarBotao(uint8_t movimento, int valor) {
  if (valor == 1) {
    comandoManual = movimento;
  } else if (comandoManual == movimento) {
    comandoManual = MOV_PARADO;
  }
}

BLYNK_WRITE(VP_BT_FRENTE) { tratarBotao(MOV_FRENTE, param.asInt()); }
BLYNK_WRITE(VP_BT_TRAS)   { tratarBotao(MOV_TRAS, param.asInt()); }
BLYNK_WRITE(VP_BT_ESQ)    { tratarBotao(MOV_ESQUERDA, param.asInt()); }
BLYNK_WRITE(VP_BT_DIR)    { tratarBotao(MOV_DIREITA, param.asInt()); }

void tarefaBlynk(void *) {
  uint32_t ultimoEnvio = 0;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      Blynk.run();
    }
    blynkConectado = Blynk.connected();

    if (blynkConectado && millis() - ultimoEnvio >= INTERVALO_BLYNK_MS) {
      ultimoEnvio = millis();
      const DadosNuvem dados = lerDadosNuvem();
      Blynk.virtualWrite(VP_FRENTE, dados.frente);
      Blynk.virtualWrite(VP_DIREITA, dados.direita);
      Blynk.virtualWrite(VP_ESQUERDA, dados.esquerda);
      Blynk.virtualWrite(VP_MOVIMENTO, NOMES_MOVIMENTO[dados.movimento]);
      Blynk.virtualWrite(VP_DESVIOS, dados.desvios);
      Blynk.virtualWrite(VP_DESVIOS_ESQ, dados.desviosEsquerda);
      Blynk.virtualWrite(VP_DESVIOS_DIR, dados.desviosDireita);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool conectarMelhorRede() {
  const int encontradas = WiFi.scanNetworks();
  int melhor = -1;
  int32_t melhorSinal = INT32_MIN;

  for (int i = 0; i < encontradas; i++) {
    for (size_t r = 0; r < sizeof(REDES_WIFI) / sizeof(REDES_WIFI[0]); r++) {
      if (WiFi.SSID(i) == REDES_WIFI[r].nome && WiFi.RSSI(i) > melhorSinal) {
        melhor = r;
        melhorSinal = WiFi.RSSI(i);
      }
    }
  }
  WiFi.scanDelete();

  if (melhor < 0) {
    Serial.println("Wi-Fi: nenhuma rede conhecida por perto");
    return false;
  }

  WiFi.begin(REDES_WIFI[melhor].nome, REDES_WIFI[melhor].senha);
  const uint32_t inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < TIMEOUT_CONEXAO_WIFI_MS) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("Wi-Fi: falha ao conectar em \"%s\" (sinal %d dBm, status %d), tentando de novo\n",
                REDES_WIFI[melhor].nome, melhorSinal, WiFi.status());
  WiFi.disconnect();
  return false;
}

void tarefaWiFi(void *) {
  bool estavaConectado = false;

  for (;;) {
    const bool conectado = WiFi.status() == WL_CONNECTED || conectarMelhorRede();
    if (conectado && !estavaConectado) {
      Serial.printf("Wi-Fi conectado em \"%s\". OTA em %s.local / IP %s\n",
                    WiFi.SSID().c_str(), OTA_NOME, WiFi.localIP().toString().c_str());
    } else if (!conectado && estavaConectado) {
      Serial.println("Wi-Fi caiu, procurando redes conhecidas...");
    }
    estavaConectado = conectado;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void tarefaOTA(void *) {
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ArduinoOTA.setHostname(OTA_NOME);
  ArduinoOTA.setPassword(OTA_SENHA);
  ArduinoOTA.onStart([]() {
    atualizandoOTA = true;
    paraCarro();
    Serial.println("OTA: recebendo programa novo, carrinho parado");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA: concluido, reiniciando"); });
  ArduinoOTA.onError([](ota_error_t erro) {
    atualizandoOTA = false;
    Serial.println("OTA: falha (erro " + String(erro) + ")");
  });
  ArduinoOTA.begin();

  for (;;) {
    ArduinoOTA.handle();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void executarComandoManual() {
  const uint8_t comando = blynkConectado ? comandoManual : MOV_PARADO;
  if (comando == codigoMovimento) {
    return;
  }

  switch (comando) {
    case MOV_FRENTE:   carroFrente(); break;
    case MOV_TRAS:     carroTras(); break;
    case MOV_ESQUERDA: carroEsquerda(); break;
    case MOV_DIREITA:  carroDireita(); break;
    default:           paraCarro(); break;
  }
}

void aguardar(uint32_t ms) {
  const uint32_t inicio = millis();
  while (millis() - inicio < ms) {
    if (modoManual && !atualizandoOTA) {
      executarComandoManual();
    }
    delay(5);
  }
}

int frenteParaDecisao(int frente, bool semLeitura) {
  if (!semLeitura) {
    ultimaFrenteValida = frente;
    memoriaFrente = true;
    return frente;
  }
  if (!memoriaFrente) {
    return frente;
  }
  ultimaFrenteValida = max(0, ultimaFrenteValida - DECAIMENTO_SEM_LEITURA);
  return ultimaFrenteValida;
}

uint8_t ladoComMaisEspaco(int direita, int esquerda) {
  return esquerda >= direita ? MOV_ESQUERDA : MOV_DIREITA;
}

uint8_t decidirMovimento(int frente, int direita, int esquerda) {
  if (frente > LIMITE_CAMINHO_LIVRE) {
    if (direita < LIMITE_LATERAL_PERTO || esquerda < LIMITE_LATERAL_PERTO) {
      return ladoComMaisEspaco(direita, esquerda);
    }
    return MOV_FRENTE;
  }
  if (frente <= DIST_MUITO_PERTO) {
    return MOV_TRAS;
  }
  if (esquerda > LIMITE_OBSTACULO || direita > LIMITE_OBSTACULO) {
    return ladoComMaisEspaco(direita, esquerda);
  }
  return MOV_TRAS;
}

void girarPulso(int direita, int esquerda) {
  if (ladoComMaisEspaco(direita, esquerda) == MOV_ESQUERDA) {
    carroEsquerda();
  } else {
    carroDireita();
  }
  aguardar(TEMPO_GIRO_MS);
  paraCarro();
  memoriaFrente = false;
}

void executarAutomatico(uint8_t acao, int direita, int esquerda) {
  if (acao == MOV_FRENTE) {
    carroFrente();
  } else if (acao == MOV_TRAS) {
    carroTras();
    aguardar(TEMPO_RE_MS);
    girarPulso(direita, esquerda);
  } else {
    girarPulso(direita, esquerda);
  }
}

void contarDesvio(bool manual, uint8_t acao, int direita, int esquerda) {
  const bool desviando = !manual && acao != MOV_FRENTE && acao != MOV_PARADO;

  if (desviando && !estavaDesviando) {
    totalDesvios++;
    if (ladoComMaisEspaco(direita, esquerda) == MOV_ESQUERDA) {
      desviosEsquerda++;
    } else {
      desviosDireita++;
    }
    salvarDesvios();
  }

  if (desviando) {
    estavaDesviando = true;
  } else if (acao == MOV_FRENTE || manual) {
    estavaDesviando = false;
  }
}

void imprimirDistancia(int distancia, bool semLeitura) {
  Serial.print(distancia);
  if (semLeitura) {
    Serial.print("(SEM_LEITURA)");
  }
}

void imprimirEstado(bool manual, uint8_t acao, int frente, bool semFrente,
                    int direita, bool semDireita, int esquerda, bool semEsquerda) {
  Serial.print(manual ? "[MANUAL] Frente: " : "[AUTO] Frente: ");
  imprimirDistancia(frente, semFrente);
  Serial.print(" | Dir: ");
  imprimirDistancia(direita, semDireita);
  Serial.print(" | Esq: ");
  imprimirDistancia(esquerda, semEsquerda);
  Serial.printf(" | Movimento: %s | Desvios: %u (esq %u, dir %u)\n",
                NOMES_MOVIMENTO[acao], totalDesvios, desviosEsquerda, desviosDireita);
}

void setup() {
  Serial.begin(9600);
  carregarDesvios();

  digitalWrite(ENA, LOW);
  digitalWrite(ENB, LOW);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  paraCarro();

  pinMode(TRIG_ESQ, OUTPUT);
  pinMode(ECHO_ESQ, INPUT);
  pinMode(TRIG_DIR, OUTPUT);
  pinMode(ECHO_DIR, INPUT);
  pinMode(TRIG_FRENTE, OUTPUT);
  pinMode(ECHO_FRENTE, INPUT);
  digitalWrite(TRIG_ESQ, LOW);
  digitalWrite(TRIG_DIR, LOW);
  digitalWrite(TRIG_FRENTE, LOW);

  analogWrite(ENA, VELOCIDADE);
  analogWrite(ENB, VELOCIDADE);

  Serial.println("Carrinho IoT iniciado. Aguardando o modo do Blynk (ate 20 s).");
  Serial.printf("Desvios salvos: %u (esq %u, dir %u)\n", totalDesvios, desviosEsquerda, desviosDireita);

  WiFi.setHostname(OTA_NOME);
  WiFi.mode(WIFI_STA);
  Blynk.config(BLYNK_AUTH_TOKEN, BLYNK_SERVIDOR, 80);
  xTaskCreatePinnedToCore(tarefaWiFi, "wifi", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(tarefaBlynk, "blynk", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(tarefaThingSpeak, "thingspeak", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(tarefaOTA, "ota", 8192, NULL, 1, NULL, 0);

  delay(300);
}

void loop() {
  if (atualizandoOTA || (!modoSincronizado && millis() < ESPERA_MODO_BLYNK_MS)) {
    paraCarro();
    delay(100);
    return;
  }

  bool semFrente, semDireita, semEsquerda;
  const int frente = lerDistancia(TRIG_FRENTE, ECHO_FRENTE, semFrente);
  aguardar(INTERVALO_SENSORES_MS);
  const int direita = lerDistancia(TRIG_DIR, ECHO_DIR, semDireita);
  aguardar(INTERVALO_SENSORES_MS);
  const int esquerda = lerDistancia(TRIG_ESQ, ECHO_ESQ, semEsquerda);
  aguardar(INTERVALO_SENSORES_MS);

  const bool manual = modoManual;
  uint8_t acao;
  if (manual) {
    executarComandoManual();
    acao = codigoMovimento;
  } else {
    acao = decidirMovimento(frenteParaDecisao(frente, semFrente), direita, esquerda);
    executarAutomatico(acao, direita, esquerda);
  }

  contarDesvio(manual, acao, direita, esquerda);

  portENTER_CRITICAL(&muxNuvem);
  dadosNuvem = {frente, direita, esquerda, acao, totalDesvios, manual, desviosEsquerda, desviosDireita};
  portEXIT_CRITICAL(&muxNuvem);

  imprimirEstado(manual, acao, frente, semFrente, direita, semDireita, esquerda, semEsquerda);
  aguardar(80);
}
