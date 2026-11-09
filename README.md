# Carrinho IoT — desvio de obstáculos com ESP32

Carrinho com 2 motores DC (ponte H L298N) e 3 sensores ultrassônicos (frente, direita, esquerda), controlado por um ESP32.

**O que ele faz:**
- **Modo automático:** anda sozinho e desvia de obstáculos.
- **Modo manual:** controlado por botões no app **Blynk** (celular ou navegador).
- Envia distâncias, movimento e número de desvios para o **Blynk** (ao vivo) e o **ThingSpeak** (histórico em gráficos).
- Conecta sozinho na melhor entre várias redes Wi-Fi cadastradas.
- Recebe programa novo **pelo Wi-Fi (OTA)**, sem cabo.

> **Origem:** a pinagem e a lógica foram levantadas primeiro pela desmontagem do firmware gravado no ESP32 (02/11/2026). Depois foi encontrado o código-fonte original (`Carrinho_ESP32_Revisado.ino`, revisão 18/10/2026), que confirmou tudo. O código atual em `carrinho_iot/` parte desse original; a versão original sem alterações está no histórico do git (commit `b13407c`). O código não tem comentários: as explicações de cada parte estão neste README.

---

## Componentes

- 1 ESP32 DevKit (30 pinos, USB-C)
- 1 ponte H L298N
- 2 rodas com motor DC
- 3 sensores ultrassônicos HC-SR04
- 2 baterias

## Ligação elétrica

### Alimentação

Borne azul de 3 parafusos da ponte H:

| Posição | Nome na placa | Liga em |
|---|---|---|
| 1 | 12V | **+** da bateria (fio vermelho) |
| 2 | GND | **−** da bateria e fio **laranja** → pino **GND** do ESP32 |
| 3 | 5V | fio **amarelo** → pino **VIN** do ESP32 |

> ⚠️ **Nunca** ligue o fio de 5 V no pino **3V3** do ESP32: ele queima. No ESP32 de 30 pinos, o VIN e o 3V3 ficam na mesma altura, colados no USB, um de cada lado. Confira o nome escrito na placa.

> ⚠️ **USB e bateria não podem alimentar o ESP32 ao mesmo tempo.** Para usar o cabo USB, **tire o fio amarelo do VIN** (o GND pode ficar). Para usar só a bateria, desconecte o USB. Para gravar programa novo com a bateria ligada, use o **OTA**.

- Os **sensores só recebem energia pela bateria.** Só com o USB, todos aparecem como `SEM_LEITURA`.
- **GND comum:** ESP32, ponte H e sensores com todos os GND ligados juntos.

### Jumpers da ponte H

- **ENA e ENB: retirados.** A velocidade vem do ESP32 (GPIO 25 → ENA, GPIO 33 → ENB). O fio vai no pino de sinal do enable. Nunca deixe o jumper e o fio ao mesmo tempo, porque isso põe o GPIO em curto com 5 V.
- **5V-EN** (perto do borne azul): **colocado**. Ele liga o regulador que gera os 5 V do ESP32 e dos sensores.

### Sensores

- VCC em 5 V; ECHO dos 3 sensores com divisor para 3,3 V, porque o ESP32 não aceita 5 V nos pinos.

## Pinagem

### Motores (ponte H)

| GPIO | Função | Ponte H |
|---|---|---|
| **25** | Velocidade (PWM) motor A | ENA |
| **26** | Direção motor A | IN1 |
| **27** | Direção motor A | IN2 |
| **33** | Velocidade (PWM) motor B | ENB |
| **32** | Direção motor B | IN3 |
| **23** | Direção motor B | IN4 |

### Sensores ultrassônicos

| Sensor | TRIG | ECHO |
|---|---|---|
| Frente | **GPIO 21** | **GPIO 22** |
| Direita | **GPIO 18** | **GPIO 19** |
| Esquerda | **GPIO 16** (RX2) | **GPIO 17** (TX2) |

Lado do D23 no ESP32, de cima para baixo: D23, D22, TX0, RX0, D21, D19, D18, D5, TX2, RX2, D4, D2, D15, GND, 3V3. Não use TX0/RX0, que são da comunicação USB.

### Movimentos

| Movimento | 26 | 27 | 32 | 23 | Velocidade |
|---|---|---|---|---|---|
| FRENTE | 0 | 1 | 0 | 1 | 180/255 |
| TRAS | 1 | 0 | 1 | 0 | 180/255 |
| ESQUERDA (gira no lugar) | 1 | 0 | 0 | 1 | 150/255 |
| DIREITA (gira no lugar) | 0 | 1 | 1 | 0 | 150/255 |
| PARADO | 0 | 0 | 0 | 0 | — |

Se uma roda girar ao contrário, os fios daquele motor estão invertidos na ponte H.

---

## Como o carrinho funciona

### Ao ligar

1. Configura pinos e motores, com o carrinho parado.
2. Começa a conectar no Wi-Fi e no Blynk, em segundo plano.
3. **Espera parado até 20 s** o Blynk informar o modo (automático ou manual). Sem isso, ele saía no automático mesmo com o app em Manual. Se o Blynk não responder em 20 s, segue no automático.

### Leitura dos sensores

- Pulso de 10 µs no TRIG e `pulseIn(ECHO, HIGH, 20000)` (timeout de 20 ms).
- Distância = duração / 58 (cm), limitada a 100 cm.
- Sem eco → vale **100 cm** e aparece `(SEM_LEITURA)` no Monitor Serial.
- Os 3 sensores são lidos um por vez, com 60 ms entre eles, para um não captar o eco do outro.

### Modo automático

| Situação | O que faz |
|---|---|
| Frente livre (**> 55 cm**) e lados longe | Anda **reto** |
| Frente livre, mas um lado **< 20 cm** | Vira para o lado oposto, para não raspar |
| Obstáculo na frente e um lado livre (**> 50 cm**) | Gira **0,25 s** para o lado com mais espaço, **para**, mede de novo e repete |
| Obstáculo a **15 cm ou menos** | **Dá ré** 0,4 s e depois gira |
| Encurralado (frente e lados bloqueados) | **Dá ré** e gira para o lado com mais espaço |

**Proteção contra batida em parede na diagonal:** quando o carrinho chega numa parede em ângulo, o eco ricocheteia e o sensor da frente fica sem leitura (que valeria 100 cm, "livre"). Enquanto anda reto, uma leitura perdida passa a valer a **última distância válida menos 15 cm** por ciclo, então ele desvia em vez de acelerar para a parede. Essa memória é apagada quando ele gira.

**Limitação:** obstáculos **mais baixos que os sensores** (rodapé, pé de cadeira, degrau) não são detectados.

Os valores ficam no começo de `carrinho_iot.ino` e podem ser ajustados:

| Constante | Valor | O que controla |
|---|---|---|
| `LIMITE_CAMINHO_LIVRE` | 55 cm | Distância da frente para considerar livre |
| `LIMITE_OBSTACULO` | 50 cm | Distância de um lado para considerar livre para girar |
| `LIMITE_LATERAL_PERTO` | 20 cm | Lado perto demais, vira mesmo com a frente livre |
| `DIST_MUITO_PERTO` | 15 cm | Abaixo disso, dá ré |
| `VELOCIDADE` | 180 | Velocidade reto e ré (0–255) |
| `VELOCIDADE_GIRO` | 150 | Velocidade nas curvas (0–255) |
| `TEMPO_GIRO_MS` | 250 ms | Duração de cada pulso de giro |
| `TEMPO_RE_MS` | 400 ms | Duração da ré |
| `DECAIMENTO_SEM_LEITURA` | 15 cm | Quanto a parede "se aproxima" a cada leitura perdida |

### Modo manual

- Botões de **segurar** no app: Frente, Trás, Esquerda, Direita. O carrinho anda enquanto o botão está apertado e para ao soltar.
- O botão é aplicado em até 5 ms depois de chegar; a demora que sobra é a da internet.
- **Se a conexão com o Blynk cair, o carrinho para.**
- No manual, o carrinho **não desvia sozinho**.

### Contagem de desvios

Cada manobra de desvio no automático (giros e/ou ré até voltar a andar reto) conta como **1 desvio**. O desvio é contado como **esquerda** ou **direita** conforme o lado para onde o carrinho virou ao começar a manobra, então **total = esquerda + direita**. Viradas no manual não contam.

Os contadores ficam **salvos na memória flash** do ESP32 (biblioteca `Preferences`, área `carrinho`) e **continuam de onde pararam** quando o carrinho é desligado e ligado de novo. Ao ligar, o Monitor Serial mostra `Desvios salvos: N (esq X, dir Y)`. A flash só é gravada quando um desvio novo acontece, para poupar a vida útil da memória.

> Para zerar os contadores, hoje é preciso apagar a flash inteira (PlatformIO: **Project Tasks → esp32 → Platform → Erase Flash**) e gravar o programa de novo pelo cabo. Isso também apaga o programa, então só faça com o cabo USB à mão.

---

## Nuvem

A internet roda em tarefas separadas no núcleo 0 do ESP32, então a demora da rede não atrapalha os sensores nem os motores.

### Blynk (ao vivo e controle)

Envia os dados a cada 2 s. Pinos virtuais do template **Carrinho IoT**:

| Pino | Nome | Tipo | Valores | Widget |
|---|---|---|---|---|
| V0 | Frente | Integer | 0–100 cm | Gauge |
| V1 | Direita | Integer | 0–100 cm | Gauge |
| V2 | Esquerda | Integer | 0–100 cm | Gauge |
| V3 | Movimento | String | PARADO / FRENTE / ESQUERDA / DIREITA / TRAS | Label |
| V4 | Desvios | Integer | 0–100000 | Label |
| V5 | Modo | Integer | 0 = automático, 1 = manual | Switch |
| V6 | Botão Frente | Integer | 0–1 | Button (**Push**) |
| V7 | Botão Trás | Integer | 0–1 | Button (**Push**) |
| V8 | Botão Esquerda | Integer | 0–1 | Button (**Push**) |
| V9 | Botão Direita | Integer | 0–1 | Button (**Push**) |
| V10 | Desvios Esquerda | Integer | 0–100000 | Label |
| V11 | Desvios Direita | Integer | 0–100000 | Label |

> ⚠️ Os botões **precisam estar em modo Push**. Em modo Switch, o carrinho continua andando depois de soltar.

**Configurar do zero:**
1. [Blynk Console](https://blynk.cloud) → **Zona do desenvolvedor → Meus templates → Novo template** (ESP32, WiFi).
2. Crie os datastreams da tabela acima.
3. Monte o **Painel da Web** com os widgets da tabela.
4. **Dispositivos → Novo dispositivo → Do template**; copie o Template ID e o token de autenticação para o `secrets.h`.

**No celular:** instale o app **Blynk IoT** (não o "Blynk Legacy"), entre com a mesma conta, abra o dispositivo, toque na **chave inglesa 🔧** e monte o painel do celular com os mesmos widgets. O painel do celular é separado do da web, mas usa os mesmos pinos.

> O plano grátis tem uma cota de **100 mil mensagens por mês** (aparece no topo do Blynk Console). Com o carrinho ligado, o uso é de cerca de 13 mil por hora (7 valores a cada 2 s), ou seja, umas 7 horas de uso por mês.

### ThingSpeak (histórico)

Envia a cada 20 s (limite do plano grátis: 1 envio a cada 15 s).

| Campo | Dado |
|---|---|
| Field 1 | Distância frente (cm) |
| Field 2 | Distância direita (cm) |
| Field 3 | Distância esquerda (cm) |
| Field 4 | Movimento: 0 = parado, 1 = frente, 2 = esquerda, 3 = direita, 4 = trás |
| Field 5 | Total de desvios |
| Field 6 | Modo: 0 = automático, 1 = manual |
| Field 7 | Desvios para a esquerda |
| Field 8 | Desvios para a direita |

Para criar: **Channels → New Channel**, marque Field 1 a 8 e copie a **Write API Key** (aba API Keys) para o `secrets.h`. Dica: nos gráficos 1 a 3, use **Y-Axis Min 0 / Max 100**.

Para testar a chave sem o ESP32, abra no navegador: `https://api.thingspeak.com/update?api_key=SUA_CHAVE&field1=42`. Se responder um número maior que 0, a chave está certa.

---

## Wi-Fi

- Cadastre todas as redes em `WIFI_REDES` no `secrets.h`. O ESP32 procura as redes por perto, conecta na **conhecida de sinal mais forte** e, se falhar ou cair, tenta de novo.
- As redes precisam ser **2,4 GHz**. No roteamento do iPhone, ative **"Maximizar compatibilidade"**.
- Cadastre o **roteamento do celular** para ter internet em qualquer lugar.
- Com sinal fraco (abaixo de −75 dBm), a conexão pode levar até 1 minuto. Chegue mais perto do roteador e não deixe fios por cima da antena do ESP32 (a ponta com o zigue-zague dourado).
- No roteador, o carrinho aparece com o nome **carrinho-iot**.

## Senhas e chaves

Nenhuma senha vai para o GitHub. Cada pessoa cria os seus arquivos localmente:

| Arquivo | Copie de | Contém |
|---|---|---|
| `carrinho_iot/secrets.h` | `carrinho_iot/secrets.example.h` | Redes Wi-Fi, chave do ThingSpeak, Template ID e token do Blynk, senha do OTA |
| `secrets.ini` (raiz) | `secrets.example.ini` | Endereço (IP) do carrinho e senha do OTA, para gravar pelo Wi-Fi |

> ⚠️ Preencha o **`secrets.h`**, não o `secrets.example.h`: o modelo vai para o GitHub. A senha do OTA tem que ser igual nos dois arquivos. Passe as senhas para o grupo por mensagem privada.

---

## Gravar o programa

### PlatformIO (VS Code) — recomendado

O projeto abre direto no VS Code com a extensão PlatformIO. A biblioteca do Blynk e a versão certa do ESP32 são instaladas sozinhas.

| Ambiente | Como grava |
|---|---|
| `env:esp32` (padrão) | Pelo cabo USB |
| `env:esp32-ota` | Pelo Wi-Fi |

**Pelo cabo:**
1. Tire o fio amarelo do VIN (ou desligue a bateria).
2. Conecte o USB e clique na **setinha →** na barra azul do VS Code.
3. Se travar em `Connecting....`, segure o botão **BOOT** do ESP32.
4. O ícone da **tomada** abre o Monitor Serial (9600).

**Pelo Wi-Fi (OTA):**
1. O ESP32 precisa já ter este código gravado uma vez pelo cabo.
2. O computador precisa estar **na mesma rede** que o carrinho.
3. Na barra azul, troque para `env:esp32-ota` e clique na setinha.
4. Se não encontrar o carrinho, atualize `endereco` no `secrets.ini` com o IP mostrado no Monitor Serial (ou na lista de aparelhos do roteador).
5. Na primeira vez, permita o Python no firewall do Windows, em rede privada.

Durante a gravação pelo Wi-Fi o carrinho **fica parado** e depois reinicia. **Deixe as rodas suspensas**, porque ele pode começar no automático se o Blynk demorar a responder. Pelo OTA não há Monitor Serial.

### Arduino IDE

Também funciona: abra a pasta `carrinho_iot`, instale a biblioteca **Blynk** (Volodymyr Shymanskyy), use o core **esp32 2.0.17** (o mesmo do carrinho) e a placa **ESP32 Dev Module**.

O PWM dos motores usa a configuração padrão do `analogWrite` (8 bits); as linhas que fixavam a frequência em 490 Hz foram retiradas.

---

## Problemas comuns

| Problema | O que fazer |
|---|---|
| **Offline no Blynk** | Espere até 1 min (sinal fraco). Confira as redes no `secrets.h`. Ligue no USB e veja o Monitor Serial: ele mostra a rede, o sinal (dBm) e o status das tentativas. |
| **Sensores em `SEM_LEITURA`** | Os sensores só funcionam com a **bateria**. Com ela ligada, confira VCC, GND, TRIG e ECHO de cada sensor. |
| **Rodas girando com o carrinho parado na mão** | É o modo automático vendo caminho livre. Mude para **Manual** no Blynk ou desligue a bateria. |
| **Uma roda gira ao contrário** | Fios daquele motor invertidos na ponte H. |
| **Bate em obstáculo baixo** | O sensor não enxerga abaixo da altura dele. Só resolve mudando a posição dos sensores. |
| **Manual demora a responder** | É a internet. Chegue mais perto do roteador. |
| **OTA não encontra o carrinho** | Confira se ele está online e na mesma rede que o computador, e atualize o IP no `secrets.ini`. |
| **ThingSpeak responde 0** | Chave errada ou envio com menos de 15 s de intervalo. |

## Histórico

| Data | Mudança |
|---|---|
| 02/11/2026 | Firmware lido e documentado; código original (18/10/2026) adicionado |
| 02/11/2026 | Wi-Fi + ThingSpeak; Blynk com modos automático e manual |
| 04/11/2026 | Projeto PlatformIO; gravação pelo Wi-Fi (OTA) |
| 04/11/2026 | Várias redes Wi-Fi, com novas tentativas automáticas |
| 04/11/2026 | Desvio em pulsos, curvas mais lentas, ré quando muito perto ou encurralado |
| 04/11/2026 | Manual mais rápido; ao ligar, espera o modo do Blynk |
| 04/11/2026 | Proteção contra batida: memória do sensor da frente e checagem lateral |
| 04/11/2026 | Contagem de desvios para a esquerda e para a direita (Blynk V10/V11, ThingSpeak Field 7/8) |
| 08/11/2026 | Contadores de desvio salvos na flash (sobrevivem ao desligar); código limpo, sem comentários |

## Backup do firmware

`firmware-backup/app0_0x10000.bin` é a cópia do programa que estava no ESP32 antes de qualquer mudança (partição `app0`). Para voltar a ele, num ESP32 que já tenha recebido um sketch Arduino:

```
esptool --port COM5 write-flash 0x10000 firmware-backup/app0_0x10000.bin
```
