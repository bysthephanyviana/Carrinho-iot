# Carrinho IoT — desvio de obstáculos com ESP32

Carrinho com 2 motores DC (ponte H tipo L298N) e 3 sensores ultrassônicos (frente, direita, esquerda), controlado por um ESP32.

> **Origem desta documentação:** a pinagem e a lógica foram levantadas primeiro pela desmontagem do firmware gravado no ESP32 (02/11/2026). Depois foi encontrado o código-fonte original (`Carrinho_ESP32_Revisado.ino`, revisão 18/10/2026), que confirmou tudo e agora está em `carrinho_iot/`.

## Componentes

- 1 ESP32
- 1 ponte H (tipo L298N)
- 2 rodas com motor DC
- 3 sensores ultrassônicos (HC-SR04)
- 2 baterias

### Ponte H: jumpers e alimentação

- **Jumpers ENA e ENB: retirados** (não confundir com o 5V-EN). A velocidade é controlada pelo ESP32 (GPIO 25 → ENA, GPIO 33 → ENB). O fio vai no pino de sinal do enable, não no pino de 5 V. Nunca deixe o jumper e o fio do ESP32 ao mesmo tempo, porque isso põe o GPIO em curto com 5 V.
- **Jumper 5V-EN** (perto do borne de alimentação): fica colocado se a bateria dos motores tiver até 12 V, porque liga o regulador interno.
- **Bateria** alimenta a ponte H e os motores. Nunca alimentar motores pelo ESP32 nem ligar ~8 V nos pinos 5V/3V3 do ESP32.
- **Com o USB ligado no notebook**, desconecte o fio externo do VIN/5V do ESP32 (mantenha o GND).
- **GND comum:** ESP32, ponte H e sensores com todos os GND ligados juntos.
- **Sensores:** VCC em 5 V regulados; ECHO dos 3 sensores com divisor para 3,3 V.

## Placa

| Item | Valor |
|---|---|
| Chip | ESP32-D0WD-V3 (rev 3.1), 4 MB flash |
| Core Arduino | esp32 2.0.17 (compilado pelo Arduino Cloud/CLI) |
| Serial | 9600 baud |

## Pinagem

### Motores (ponte H)

| GPIO | Função no código | Pino da ponte H |
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

`carrinho_iot/carrinho_iot.ino` é o código original usado no carrinho (revisão 18/10/2026). Abra a pasta `carrinho_iot` no Arduino IDE, escolha a placa **ESP32 Dev Module** e envie.

> Use o core **esp32 2.0.x** (o carrinho foi compilado com 2.0.17). No core 3.x, `analogWriteResolution` e `analogWriteFrequency` recebem também o pino e o código não compila sem ajuste.

A constante `SEM_LEITURA_COMO_100` controla o que acontece quando um sensor não responde: `true` (padrão, igual ao Arduino original) trata como 100 cm; `false` faz o carrinho parar quando o sensor da frente não responde.

## Nuvem (ThingSpeak)

O carrinho envia os dados a cada 20 s para o [ThingSpeak](https://thingspeak.mathworks.com), que é gratuito e já mostra os gráficos. O envio roda numa tarefa separada (núcleo 0), então a demora da internet não atrapalha os sensores. Sem Wi-Fi, o carrinho funciona normalmente e tenta reconectar sozinho.

| Campo | Dado |
|---|---|
| Field 1 | Distância frente (cm) |
| Field 2 | Distância direita (cm) |
| Field 3 | Distância esquerda (cm) |
| Field 4 | Movimento: 0 = parado, 1 = frente, 2 = esquerda, 3 = direita |
| Field 5 | Total de desvios desde que ligou |

Cada vez que o carrinho **começa** a virar (esquerda ou direita) conta como 1 desvio. O total volta a 0 quando o ESP32 reinicia.

### Configurar

1. Crie uma conta no ThingSpeak → **Channels → New Channel**, marque Field 1 a 5 com os nomes da tabela acima e salve.
2. Na aba **API Keys**, copie a **Write API Key**.
3. Na pasta `carrinho_iot`, copie `secrets.example.h` para `secrets.h` e preencha a rede Wi-Fi (precisa ser **2,4 GHz**; no celular, ative o modo compatível) e a chave.
4. Envie o código e abra o Monitor Serial (9600): deve aparecer `Nuvem: enviado, registro N`.

> `secrets.h` está no `.gitignore` e **não deve ir para o GitHub**. Cada pessoa cria o seu localmente.

## Backup do firmware

`firmware-backup/app0_0x10000.bin` é a cópia do programa que estava no ESP32 (partição `app0`, esquema de partições padrão do Arduino). Para regravar num ESP32 que já tenha recebido um sketch Arduino:

```
esptool --port COM5 write-flash 0x10000 firmware-backup/app0_0x10000.bin
```
