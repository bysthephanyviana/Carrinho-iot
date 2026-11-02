// Carrinho IoT - desvio de obstaculos com ESP32
//
// Reconstruido a partir do firmware gravado no ESP32 (02/11/2026).
// Pinos, tempos, limites e mensagens sao os mesmos do original.
// Detalhes da pinagem e da logica: README.md na raiz do repositorio.

// ---------- Motores (ponte H) ----------
const int ENA = 25;  // PWM motor A
const int IN1 = 26;
const int IN2 = 27;
const int ENB = 33;  // PWM motor B
const int IN3 = 32;
const int IN4 = 23;

const int VELOCIDADE = 180;  // 0..255
const int PWM_FREQ = 490;    // Hz, igual ao Arduino UNO
const int PWM_BITS = 8;

// ---------- Sensores ultrassonicos ----------
const int TRIG_FRENTE = 21;
const int ECHO_FRENTE = 22;
const int TRIG_DIR = 18;
const int ECHO_DIR = 19;
const int TRIG_ESQ = 16;
const int ECHO_ESQ = 17;

const unsigned long TIMEOUT_ECHO = 20000;  // us
const int SEM_LEITURA = 100;               // cm assumidos quando nao ha eco
const int DIST_MAX = 100;                  // cm

// ---------- Decisao ----------
const int LIMITE_FRENTE = 55;  // cm
const int LIMITE_LADO = 50;    // cm

const char *movimento = "PARADO";

// ---------- Movimentos ----------
void frente() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  movimento = "FRENTE";
}

void esquerda() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  movimento = "ESQUERDA";
}

void direita() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  movimento = "DIREITA";
}

void parar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  movimento = "PARADO";
}

// ---------- Sensores ----------
// Retorna a distancia em cm (1..100). Sem eco: retorna SEM_LEITURA e marca semLeitura.
int lerDistancia(int trig, int echo, bool &semLeitura) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  unsigned long duracao = pulseIn(echo, HIGH, TIMEOUT_ECHO);
  unsigned long cm = duracao / 58;

  semLeitura = (duracao == 0 || cm < 1);
  if (semLeitura) return SEM_LEITURA;
  return min(cm, (unsigned long)DIST_MAX);
}

void imprimirDistancia(int cm, bool semLeitura) {
  Serial.print(cm, DEC);
  if (semLeitura) Serial.print("(SEM_LEITURA)");
}

// ---------- PWM ----------
void configurarPWM() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  analogWriteResolution(ENA, PWM_BITS);
  analogWriteResolution(ENB, PWM_BITS);
  analogWriteFrequency(ENA, PWM_FREQ);
  analogWriteFrequency(ENB, PWM_FREQ);
#else
  analogWriteResolution(PWM_BITS);
  analogWriteFrequency(PWM_FREQ);
#endif
}

void setup() {
  Serial.begin(9600);

  digitalWrite(ENA, LOW);
  digitalWrite(ENB, LOW);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  parar();

  pinMode(TRIG_ESQ, OUTPUT);
  pinMode(ECHO_ESQ, INPUT);
  pinMode(TRIG_DIR, OUTPUT);
  pinMode(ECHO_DIR, INPUT);
  pinMode(TRIG_FRENTE, OUTPUT);
  pinMode(ECHO_FRENTE, INPUT);
  digitalWrite(TRIG_ESQ, LOW);
  digitalWrite(TRIG_DIR, LOW);
  digitalWrite(TRIG_FRENTE, LOW);

  configurarPWM();
  analogWrite(ENA, VELOCIDADE);
  analogWrite(ENB, VELOCIDADE);

  Serial.println("INICIO - ESP32 compatibilidade - PWM 180/255");
  Serial.println("ATENCAO: SEM_LEITURA vale 100 cm, como no Arduino.");
  Serial.println("Sensor desconectado pode parecer caminho livre.");
  Serial.println("Teste inicial com rodas suspensas. Serial: 9600.");
  delay(300);
}

void loop() {
  bool semFrente = false, semDir = false, semEsq = false;

  // Intervalo entre leituras evita que um sensor capte o eco do outro
  int distFrente = lerDistancia(TRIG_FRENTE, ECHO_FRENTE, semFrente);
  delay(60);
  int distDir = lerDistancia(TRIG_DIR, ECHO_DIR, semDir);
  delay(60);
  int distEsq = lerDistancia(TRIG_ESQ, ECHO_ESQ, semEsq);
  delay(60);

  if (distFrente > LIMITE_FRENTE) {
    frente();
  } else if (distEsq > LIMITE_LADO || distDir > LIMITE_LADO) {
    if (distEsq < distDir) direita();
    else esquerda();
  } else {
    parar();
  }

  Serial.print("Frente: ");
  imprimirDistancia(distFrente, semFrente);
  Serial.print(" | Dir: ");
  imprimirDistancia(distDir, semDir);
  Serial.print(" | Esq: ");
  imprimirDistancia(distEsq, semEsq);
  Serial.print(" | Movimento: ");
  Serial.println(movimento);

  delay(80);
}
