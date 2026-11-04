/*
  CARRINHO ESP32-WROOM-32 - VERSAO DE COMPATIBILIDADE PARA TESTE
  Revisao: 18/10/2026 - PWM corrigido para Arduino-ESP32 2.0.17

  Mantem os 12 pinos, PWM 180/255, limites de 55/50 cm e as
  combinacoes IN1..IN4 do codigo original do Arduino.

  ALTERACOES EM RELACAO AO ESP32 REVISADO ANTERIOR:
  1. Leitura ausente volta a valer 100 cm, como no original.
     O Monitor Serial identifica esses valores como SEM_LEITURA.
     Isso NAO comprova caminho livre: sensor desligado tambem pode
     produzir essa indicacao. Teste primeiro com rodas suspensas.
  2. Intervalo de 60 ms entre sensores para reduzir a possibilidade
     de um sensor receber o som do outro. O ciclo fica mais lento.
  3. Monitor Serial mostra as distancias E o movimento comandado.

  Nao e garantia de equivalencia das medicoes nem de funcionamento
  fisico. Ultrasonic.h foi substituida por pulseIn(), como antes.
  Se a frente esta livre, obstaculo lateral isolado NAO causa parada:
  esta e a regra do programa original.

  ANTES DO TESTE:
  - ECHO esquerdo/direito/frontal com divisor para nivel de 3,3 V.
  - VCC dos sensores em 5 V regulados; todos os GND em comum.
  - Retirar jumpers ENA e ENB da L298N (nao confundir com 5V-EN).
  - Com USB no notebook, desconectar o fio externo do VIN/5V do
    ESP32. Manter GND comum. Bateria alimenta driver e motores.
  - Nao alimentar motores pelo ESP32 nem aplicar 8 V em 5V/3V3.
  - Monitor Serial: 9600 baud.

  API PWM: Arduino-ESP32 2.0.17, conforme o ambiente de compilacao.
  analogWriteResolution(bits) e analogWriteFrequency(Hz) recebem
  apenas UM argumento nesta versao e configuram o PWM globalmente.

  NUVEM (02/11/2026):
  Envia distancias, movimento e total de desvios ao ThingSpeak
  (historico) e ao Blynk (app ao vivo).
  Pelo app Blynk o carrinho alterna entre modo AUTOMATICO (regra
  original de desvio) e MANUAL (botoes frente/tras/esquerda/direita).
  No modo manual, se a conexao com o Blynk cair, o carrinho para.
  Internet roda em tarefas separadas no nucleo 0, para que a
  demora da rede nao trave a leitura dos sensores no loop().
  Rede e chaves ficam em secrets.h (copie de secrets.example.h).
  Biblioteca necessaria: "Blynk" (Volodymyr Shymanskyy), 1.3 ou mais nova.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include "secrets.h"  // precisa vir antes do Blynk (define o template)
#include <BlynkSimpleEsp32.h>

const uint8_t TRIG_ESQ   = 16; // RX2
const uint8_t ECHO_ESQ   = 17; // TX2, atraves do divisor
const uint8_t TRIG_DIR   = 18; // D18
const uint8_t ECHO_DIR   = 19; // D19, atraves do divisor
const uint8_t TRIG_FRONT = 21; // D21
const uint8_t ECHO_FRONT = 22; // D22, atraves do divisor

const uint8_t ENA = 25;
const uint8_t IN1 = 26;
const uint8_t IN2 = 27;
const uint8_t ENB = 33;
const uint8_t IN3 = 32;
const uint8_t IN4 = 23;

const uint8_t VELOCIDADE = 180;
const uint32_t PWM_HZ = 490;
const int LIMITE_OBSTACULO = 50;
const int LIMITE_CAMINHO_LIVRE = 55;
const uint32_t TIMEOUT_ECHO_US = 20000UL;
const uint32_t INTERVALO_SENSORES_MS = 60;

// true: reproduz a regra original para ausencia de leitura (100 cm).
// false: leitura ausente vale -1; frontal ausente para o carrinho,
//        lateral ausente nao e considerada uma opcao de desvio.
const bool SEM_LEITURA_COMO_100 = true;

const char *movimentoAtual = "PARADO";

// Codigo numerico do movimento, para o grafico da nuvem.
const uint8_t MOV_PARADO = 0;
const uint8_t MOV_FRENTE = 1;
const uint8_t MOV_ESQUERDA = 2;
const uint8_t MOV_DIREITA = 3;
const uint8_t MOV_TRAS = 4;  // so no modo manual
uint8_t codigoMovimento = MOV_PARADO;

// Alterados pela tarefa do Blynk, lidos pelo loop().
volatile bool modoManual = false;
volatile uint8_t comandoManual = MOV_PARADO;
volatile bool blynkConectado = false;

// Gravacao pelo Wi-Fi: no PlatformIO, ambiente "esp32-ota".
const char *const OTA_NOME = "carrinho-iot";  // carrinho-iot.local na rede
volatile bool atualizandoOTA = false;

// Um desvio = cada vez que o carrinho COMECA a virar.
uint32_t totalDesvios = 0;
bool estavaDesviando = false;

const char *const NOMES_MOVIMENTO[] = {"PARADO", "FRENTE", "ESQUERDA", "DIREITA", "TRAS"};

// ThingSpeak gratuito aceita no maximo 1 envio a cada 15 s.
const uint32_t INTERVALO_THINGSPEAK_MS = 20000;
// Blynk: app ao vivo. Intervalo curto gasta a cota mensal do plano gratis.
const uint32_t INTERVALO_BLYNK_MS = 2000;

// Pinos virtuais do Blynk (datastreams do template)
// V0..V4: carrinho -> app   |   V5..V9: app -> carrinho
#define VP_FRENTE    V0
#define VP_DIREITA   V1
#define VP_ESQUERDA  V2
#define VP_MOVIMENTO V3
#define VP_DESVIOS   V4
#define VP_MODO      V5  // 0 = automatico, 1 = manual
#define VP_BT_FRENTE V6
#define VP_BT_TRAS   V7
#define VP_BT_ESQ    V8
#define VP_BT_DIR    V9

struct DadosNuvem {
  int frente;
  int direita;
  int esquerda;
  uint8_t movimento;
  uint32_t desvios;
  bool manual;
};
DadosNuvem dadosNuvem = {0, 0, 0, MOV_PARADO, 0, false};
portMUX_TYPE muxNuvem = portMUX_INITIALIZER_UNLOCKED;

void tarefaBlynk(void *parametro);
void tarefaThingSpeak(void *parametro);
void tarefaOTA(void *parametro);
void tarefaWiFi(void *parametro);

// Redes conhecidas: o ESP32 conecta na de sinal mais forte.
// secrets.h antigo (so WIFI_SSID/WIFI_SENHA) continua funcionando.
struct RedeWiFi {
  const char *nome;
  const char *senha;
};
#ifdef WIFI_REDES
const RedeWiFi REDES_WIFI[] = WIFI_REDES;
#else
const RedeWiFi REDES_WIFI[] = {{WIFI_SSID, WIFI_SENHA}};
#endif
const uint32_t TIMEOUT_CONEXAO_WIFI_MS = 20000;
void paraCarro();
void carroTras();
void carroFrente();
void carroEsquerda();
void carroDireita();
int lerDistancia(uint8_t trigPin, uint8_t echoPin, bool &semLeitura);
void imprimirDistancia(int distancia, bool semLeitura);

int lerDistancia(uint8_t trigPin, uint8_t echoPin, bool &semLeitura) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  const uint32_t duracao = pulseIn(echoPin, HIGH, TIMEOUT_ECHO_US);
  const int distancia = static_cast<int>(duracao / 58UL);
  semLeitura = (duracao == 0 || distancia <= 0);

  if (semLeitura) {
    return SEM_LEITURA_COMO_100 ? 100 : -1;
  }
  return distancia > 100 ? 100 : distancia;
}

void setup() {
  Serial.begin(9600);

  // Desabilita os motores antes de configurar seus comandos.
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
  pinMode(TRIG_FRONT, OUTPUT);
  pinMode(ECHO_FRONT, INPUT);
  digitalWrite(TRIG_ESQ, LOW);
  digitalWrite(TRIG_DIR, LOW);
  digitalWrite(TRIG_FRONT, LOW);

  // Na versao 2.0.17, estas configuracoes nao recebem o pino.
  // Configurar antes do primeiro analogWrite, para ambos os motores.
  analogWriteResolution(8);
  analogWriteFrequency(PWM_HZ);
  analogWrite(ENA, VELOCIDADE);
  analogWrite(ENB, VELOCIDADE);

  Serial.println("INICIO - ESP32 compatibilidade - PWM 180/255");
  if (SEM_LEITURA_COMO_100) {
    Serial.println("ATENCAO: SEM_LEITURA vale 100 cm, como no Arduino.");
    Serial.println("Sensor desconectado pode parecer caminho livre.");
  } else {
    Serial.println("Frontal SEM_LEITURA provoca parada.");
  }
  Serial.println("Teste inicial com rodas suspensas. Serial: 9600.");

  // Nao espera conectar: o carrinho funciona mesmo sem internet.
  WiFi.setHostname(OTA_NOME);  // nome que aparece no roteador
  WiFi.mode(WIFI_STA);
  xTaskCreatePinnedToCore(tarefaWiFi, "wifi", 8192, NULL, 1, NULL, 0);
  Blynk.config(BLYNK_AUTH_TOKEN, BLYNK_SERVIDOR, 80);
  // Tarefas separadas: se o ThingSpeak demorar, o controle manual nao trava.
  xTaskCreatePinnedToCore(tarefaBlynk, "blynk", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(tarefaThingSpeak, "thingspeak", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(tarefaOTA, "ota", 8192, NULL, 1, NULL, 0);

  delay(300);
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

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(url);
  const int codigoHttp = http.GET();
  // ThingSpeak responde o numero do registro; "0" significa recusado.
  const String resposta = codigoHttp > 0 ? http.getString() : "";
  http.end();

  if (codigoHttp == 200 && resposta != "0") {
    Serial.println("ThingSpeak: enviado, registro " + resposta);
  } else {
    Serial.println("ThingSpeak: falha no envio (HTTP " + String(codigoHttp) + ")");
  }
}

void tarefaThingSpeak(void *parametro) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    portENTER_CRITICAL(&muxNuvem);
    const DadosNuvem dados = dadosNuvem;
    portEXIT_CRITICAL(&muxNuvem);

    enviarThingSpeak(dados);
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_THINGSPEAK_MS));
  }
}

// ---------- Blynk: comandos vindos do app ----------

// Ao (re)conectar, busca no servidor o estado atual do botao de modo.
BLYNK_CONNECTED() {
  Blynk.syncVirtual(VP_MODO);
}

BLYNK_WRITE(VP_MODO) {
  modoManual = param.asInt() == 1;
  comandoManual = MOV_PARADO;
  Serial.println(modoManual ? "Blynk: modo MANUAL" : "Blynk: modo AUTOMATICO");
}

// Botoes em modo "Push": 1 ao apertar, 0 ao soltar.
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

void tarefaBlynk(void *parametro) {
  uint32_t ultimoEnvio = 0;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      Blynk.run();  // conecta, reconecta e recebe os comandos do app
    }
    blynkConectado = Blynk.connected();

    if (blynkConectado && millis() - ultimoEnvio >= INTERVALO_BLYNK_MS) {
      ultimoEnvio = millis();

      portENTER_CRITICAL(&muxNuvem);
      const DadosNuvem dados = dadosNuvem;
      portEXIT_CRITICAL(&muxNuvem);

      Blynk.virtualWrite(VP_FRENTE, dados.frente);
      Blynk.virtualWrite(VP_DIREITA, dados.direita);
      Blynk.virtualWrite(VP_ESQUERDA, dados.esquerda);
      Blynk.virtualWrite(VP_MOVIMENTO, NOMES_MOVIMENTO[dados.movimento]);
      Blynk.virtualWrite(VP_DESVIOS, dados.desvios);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Executa o botao apertado no app. Sem conexao, para por seguranca.
void executarComandoManual() {
  const uint8_t comando = blynkConectado ? comandoManual : MOV_PARADO;

  if (comando == MOV_FRENTE) {
    carroFrente();
  } else if (comando == MOV_TRAS) {
    carroTras();
  } else if (comando == MOV_ESQUERDA) {
    carroEsquerda();
  } else if (comando == MOV_DIREITA) {
    carroDireita();
  } else {
    paraCarro();
  }
}

// Procura as redes, escolhe a conhecida de sinal mais forte e conecta.
// (WiFiMulti falhava neste roteador por conectar presa a um BSSID.)
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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Wi-Fi: falha ao conectar em \"");
    Serial.print(REDES_WIFI[melhor].nome);
    Serial.print("\" (sinal ");
    Serial.print(melhorSinal);
    Serial.print(" dBm, status ");
    Serial.print(WiFi.status());
    Serial.println("), tentando de novo");
    WiFi.disconnect();
    return false;
  }
  return true;
}

// Mantem o Wi-Fi: se cair, procura de novo em todas as redes conhecidas.
void tarefaWiFi(void *parametro) {
  bool estavaConectado = false;

  for (;;) {
    const bool conectado = WiFi.status() == WL_CONNECTED || conectarMelhorRede();

    if (conectado && !estavaConectado) {
      Serial.print("Wi-Fi conectado em \"");
      Serial.print(WiFi.SSID());
      Serial.print("\". OTA em ");
      Serial.print(OTA_NOME);
      Serial.print(".local / IP ");
      Serial.println(WiFi.localIP());
    } else if (!conectado && estavaConectado) {
      Serial.println("Wi-Fi caiu, procurando redes conhecidas...");
    }
    estavaConectado = conectado;

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void tarefaOTA(void *parametro) {
  // O OTA so pode comecar depois que o Wi-Fi conectar.
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
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA: concluido, reiniciando");
  });
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

void loop() {
  // Durante a gravacao pelo Wi-Fi o carrinho fica parado.
  if (atualizandoOTA) {
    paraCarro();
    delay(100);
    return;
  }

  bool semFrente = false;
  bool semDireita = false;
  bool semEsquerda = false;

  const int frente = lerDistancia(TRIG_FRONT, ECHO_FRONT, semFrente);
  delay(INTERVALO_SENSORES_MS);
  const int direita = lerDistancia(TRIG_DIR, ECHO_DIR, semDireita);
  delay(INTERVALO_SENSORES_MS);
  const int esquerda = lerDistancia(TRIG_ESQ, ECHO_ESQ, semEsquerda);
  delay(INTERVALO_SENSORES_MS);

  const bool manual = modoManual;

  if (manual) {
    executarComandoManual();
  } else if (frente < 0) {
    paraCarro();
  } else if (frente > LIMITE_CAMINHO_LIVRE) {
    carroFrente();
  } else if (esquerda > LIMITE_OBSTACULO || direita > LIMITE_OBSTACULO) {
    if (esquerda >= direita) {
      carroEsquerda();
    } else {
      carroDireita();
    }
  } else {
    paraCarro();
  }

  // So conta desvios feitos sozinho (modo automatico).
  const bool desviando = !manual &&
      (codigoMovimento == MOV_ESQUERDA || codigoMovimento == MOV_DIREITA);
  if (desviando && !estavaDesviando) {
    totalDesvios++;
  }
  estavaDesviando = desviando;

  portENTER_CRITICAL(&muxNuvem);
  dadosNuvem = {frente, direita, esquerda, codigoMovimento, totalDesvios, manual};
  portEXIT_CRITICAL(&muxNuvem);

  Serial.print(manual ? "[MANUAL] " : "[AUTO] ");
  Serial.print("Frente: ");
  imprimirDistancia(frente, semFrente);
  Serial.print(" | Dir: ");
  imprimirDistancia(direita, semDireita);
  Serial.print(" | Esq: ");
  imprimirDistancia(esquerda, semEsquerda);
  Serial.print(" | Movimento: ");
  Serial.print(movimentoAtual);
  Serial.print(" | Desvios: ");
  Serial.println(totalDesvios);
  delay(80);
}

void imprimirDistancia(int distancia, bool semLeitura) {
  Serial.print(distancia);
  if (semLeitura) {
    Serial.print("(SEM_LEITURA)");
  }
}

void carroFrente() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  movimentoAtual = "FRENTE";
  codigoMovimento = MOV_FRENTE;
}

void carroEsquerda() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  movimentoAtual = "ESQUERDA";
  codigoMovimento = MOV_ESQUERDA;
}

void carroDireita() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  movimentoAtual = "DIREITA";
  codigoMovimento = MOV_DIREITA;
}

void carroTras() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  movimentoAtual = "TRAS";
  codigoMovimento = MOV_TRAS;
}

void paraCarro() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  movimentoAtual = "PARADO";
  codigoMovimento = MOV_PARADO;
}
