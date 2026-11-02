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
*/

#include <Arduino.h>

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

void paraCarro();
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
  delay(300);
}

void loop() {
  bool semFrente = false;
  bool semDireita = false;
  bool semEsquerda = false;

  const int frente = lerDistancia(TRIG_FRONT, ECHO_FRONT, semFrente);
  delay(INTERVALO_SENSORES_MS);
  const int direita = lerDistancia(TRIG_DIR, ECHO_DIR, semDireita);
  delay(INTERVALO_SENSORES_MS);
  const int esquerda = lerDistancia(TRIG_ESQ, ECHO_ESQ, semEsquerda);
  delay(INTERVALO_SENSORES_MS);

  if (frente < 0) {
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

  Serial.print("Frente: ");
  imprimirDistancia(frente, semFrente);
  Serial.print(" | Dir: ");
  imprimirDistancia(direita, semDireita);
  Serial.print(" | Esq: ");
  imprimirDistancia(esquerda, semEsquerda);
  Serial.print(" | Movimento: ");
  Serial.println(movimentoAtual);
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
}

void carroEsquerda() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  movimentoAtual = "ESQUERDA";
}

void carroDireita() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  movimentoAtual = "DIREITA";
}

void paraCarro() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  movimentoAtual = "PARADO";
}
