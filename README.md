# Controlador de Lâmpada LED RGBW ESP32

Um controlador de lâmpada LED RGBW baseado em ESP32 com interface Web. Este projeto permite controlar a intensidade de canais de cores individuais e ativar efeitos visuais dinâmicos através de qualquer navegador conectado à mesma rede Wi-Fi.

## Funcionalidades

* **Interface HTML:**
    * **Controle Individual de Canais:** Ajuste fino de intensidade (0-4095) para Vermelho, Verde, Azul e Branco.
    * **Efeitos Dinâmicos:**
        * **Rainbow Cycle:** Ciclo de cores suave (RGB).
        * **Fire Flicker:** Simulação de chama de fogo.
        * **Breathing:** Efeito de respiração (fade in/out) em uma cor específica.
        * **Strobe:** Pisca a luz rapidamente (frequência ajustável).
        * **Chase:** Sequenciamento dos LEDs.
    * **Multitarefa (Non-blocking):** O código usa `millis()` para gerenciar efeitos sem travar o servidor web.

## Hardware Necessário

* **Microcontrolador:** ESP32 (Testado no modelo ESP32 DOIT DevKit V4, mas funciona no V1 e no EspDevModule normalmente).
* **LEDs:** Módulo ou fita de LED RGBW (ou RGB).
* **Alimentação:** Bateria Li-Ion e/ou fonte externa.

### Pinagem (GPIOs)

Conforme definido em `src/main.cpp`:

| Cor | GPIO | Pino na Placa |
| :--- | :--- | :--- |
| **Vermelho (R)** | `33` | GPIO 33 |
| **Verde (G)** | `32` | GPIO 32 |
| **Azul (B)** | `25` | GPIO 25 |
| **Branco (W)** | `26` | GPIO 26 |

> **Nota:** O código usa PWM de 12 bits (valores de 0 a 4095) para um controle de brilho mais suave.

## Como Instalar e Rodar

### 1. Pré-requisitos
Certifique-se de ter:
* Instalado o [VS Code](https://code.visualstudio.com/).
* Instalado a extensão [PlatformIO IDE](https://platformio.org/) no VS Code.
* Clonado este repositório e estar com a pasta aberta no VS Code.

### 2. Configurar Wi-Fi
O arquivo com as senhas não é enviado para o Git por segurança.

1.  Crie um arquivo chamado `secrets.h` na pasta raiz do projeto (mesma pasta do `platformio.ini`).
2.  Cole o código abaixo e altere para sua rede:

```c
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

#define WIFI_SSID "NOME_DA_SUA_REDE"
#define WIFI_PASSWORD "SUA_SENHA"

#endif

```

### 3. Compilar e Upload
    1. Conecte o ESP32 via USB.
    2. No PlatformIO (barra lateral esquerda, ícone de formiga ou barra inferior), clique em **Upload** (seta para a direita `→`).
    3. Aguarde a compilação e gravação.

### 4. Acessar o Painel
    1. Abra o **Serial Monitor** (ícone de tomada na barra inferior do PlatformIO) e certifique-se que o baud rate está em `115200`.
    2. Reinicie o ESP32 (pressione o botão EN/RST na placa).
    3. O IP atribuído aparecerá no terminal (ex: `192.168.1.105`).
    4. Digite esse IP no navegador do seu celular ou PC.

## Estrutura do Projeto

```text
.
├── images/                  # Fotos do projeto
│   └── montagem_eletrica... # Foto da montagem física
│   └── montagem_completa... # Foto da montagem completa
│   └── teste_rainbow...     # Gif do Teste RGB Montado
├── include/                 # Headers globais
├── lib/                     # Bibliotecas privadas
├── src/                     # Código fonte principal
│   └── main.cpp             # Lógica do servidor web e controle de LEDs
├── secrets.h                # Credenciais Wi-Fi (Crie este arquivo)
├── platformio.ini           # Configuração do PlatformIO (Board, Baud rate)
└── README.md                # Documentação do projeto
```

## API
Você pode integrar este projeto com automação (Home Assistant, Node-RED) usando requisições HTTP GET:

* **Ajustar Cor:** `/control?r_intensity=4095&g_intensity=0...`
* **Iniciar Efeito:** `/control?effect=rainbow&speed=30`
* **Parar Tudo:** `/control?r_intensity=0&g_intensity=0&b_intensity=0&w_intensity=0`

## Notas sobre a Montagem Física
* Certifique-se de que a alimentação (Bateria/USB) é suficiente para a corrente total de todos os LEDs ligados em branco total (4095) para evitar *brownouts* (quedas de energia) no ESP32.
* **Dica:** A utilização de um capacitor eletrolítico (ex: 100uF ou 1000uF) na entrada de energia (entre VCC e GND, perto dos LEDs) é bem-vinda para estabilizar a tensão, caso a fonte sofra oscilações com a mudança brusca de brilho.