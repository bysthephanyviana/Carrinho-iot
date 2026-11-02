# Carrinho IoT — desvio de obstáculos com ESP32

Carrinho com 2 motores DC (ponte H tipo L298N) e 3 sensores ultrassônicos (frente, direita, esquerda), controlado por um ESP32.

> **Origem desta documentação:** o código-fonte original (`.ino`) não estava disponível. Tudo abaixo foi levantado lendo o firmware gravado no ESP32 (dump da flash + desmontagem do binário) em 02/11/2026. Os números de GPIO e a lógica são exatos; os nomes "ENA/IN1/…" e qual motor é "esquerdo/direito" são **dedução** pela ordem do código — confirme olhando a fiação.

## Componentes

- 1 ESP32
- 1 ponte H (tipo L298N)
- 2 rodas com motor DC
- 3 sensores ultrassônicos (HC-SR04)
- 2 baterias

### Ponte H: jumpers e alimentação

- **Jumpers ENA e ENB: retirados.** A velocidade é controlada pelo ESP32 (GPIO 25 → ENA, GPIO 33 → ENB). O fio vai no pino de sinal do enable, não no pino de 5 V. Nunca deixe o jumper e o fio do ESP32 ao mesmo tempo, porque isso põe o GPIO em curto com 5 V.
- **Jumper 5V-EN** (perto do borne de alimentação): fica colocado se a bateria dos motores tiver até 12 V, porque liga o regulador interno.
- **GND comum:** o GND do ESP32 precisa estar ligado ao GND da ponte H, senão os sinais de direção e PWM não funcionam.

## Placa

| Item | Valor |
|---|---|
| Chip | ESP32-D0WD-V3 (rev 3.1), 4 MB flash |
| Core Arduino | esp32 2.0.17 (compilado pelo Arduino Cloud/CLI) |
| Serial | 9600 baud |

## Pinagem

### Motores (ponte H)

| GPIO | Função no código | Provável pino da ponte H |
|---|---|---|
| **25** | PWM (`analogWrite` 180/255) | ENA |
| **26** | direção motor A | IN1 |
| **27** | direção motor A | IN2 |
| **33** | PWM (`analogWrite` 180/255) | ENB |
| **32** | direção motor B | IN3 |
| **23** | direção motor B | IN4 |

### Sensores ultrassônicos (HC-SR04)

| Sensor | TRIG | ECHO |
|---|---|---|
| Frente | **GPIO 21** | **GPIO 22** |
| Direita | **GPIO 18** | **GPIO 19** |
| Esquerda | **GPIO 16** | **GPIO 17** |

> ⚠️ O HC-SR04 trabalha em 5 V e o pino ECHO devolve 5 V. O ESP32 aceita só 3,3 V — use divisor resistivo ou módulo 3,3 V no ECHO.

## Movimentos

| Movimento | 26 | 27 | 32 | 23 |
|---|---|---|---|---|
| FRENTE | 0 | 1 | 0 | 1 |
| ESQUERDA (gira no lugar) | 1 | 0 | 0 | 1 |
| DIREITA (gira no lugar) | 0 | 1 | 1 | 0 |
| PARADO | 0 | 0 | 0 | 0 |

Se o carrinho andar para trás em vez de frente, ou girar para o lado errado, os fios de um motor estão invertidos em relação ao que o código espera.

## Lógica do programa

**setup()**
1. `Serial.begin(9600)`
2. Pinos dos motores como saída, carrinho parado
3. TRIG como saída (LOW), ECHO como entrada
4. PWM 8 bits, ~490 Hz, velocidade 180/255 nos dois motores
5. Imprime mensagens de início e espera 300 ms

**Leitura de cada sensor**
- Pulso de 10 µs no TRIG, `pulseIn(ECHO, HIGH, 20000)` (timeout 20 ms)
- Distância = duração / 58 (cm), limitada a no máximo 100 cm
- Sem eco (timeout ou < 1 cm) → vale **100 cm** e aparece `(SEM_LEITURA)` no serial

**loop()** — a cada ~260 ms
1. Lê frente → espera 60 ms → direita → 60 ms → esquerda → 60 ms
2. Decide:
   - frente **> 55 cm** → FRENTE
   - senão, se esquerda **> 50** ou direita **> 50**:
     - esquerda < direita → DIREITA
     - senão → ESQUERDA
   - senão → PARADO
3. Imprime `Frente: X | Dir: Y | Esq: Z | Movimento: ...` e espera 80 ms

> ⚠️ Como "sem leitura" vale 100 cm, **sensor desconectado = caminho livre**: o carrinho segue em frente e bate. Teste com as rodas suspensas e confira no monitor serial se nenhum sensor mostra `(SEM_LEITURA)`.

## Código

`carrinho_iot/carrinho_iot.ino` foi reescrito a partir do firmware. Usa os mesmos pinos, tempos, limites e mensagens do original. Abra a pasta `carrinho_iot` no Arduino IDE, escolha a placa **ESP32 Dev Module** e envie. Funciona com o core ESP32 2.x (usado no carrinho) e 3.x.

## Backup do firmware

`firmware-backup/app0_0x10000.bin` é a cópia do programa que estava no ESP32 (partição `app0`, esquema de partições padrão do Arduino). Para regravar num ESP32 que já tenha recebido um sketch Arduino:

```
esptool --port COM5 write-flash 0x10000 firmware-backup/app0_0x10000.bin
```
