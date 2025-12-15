# Controlador de Lâmpada LED RGBW ESP32

Um controlador de lâmpada LED RGBW baseado em ESP32 com interface Web. Este projeto permite controlar a intensidade de canais de cores individuais e ativar efeitos visuais dinâmicos através de qualquer navegador conectado à mesma rede Wi-Fi, além de enviar telemetria via MQTT.

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
* **Telemetria MQTT:** Envio de dados de **Temperatura** e **Umidade** (do sensor DHT, se conectado) para um broker MQTT.

## Hardware Necessário

* **Microcontrolador:** ESP32 (Testado no modelo ESP32 DOIT DevKit V4).
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

---

## Como Instalar e Rodar

### 1. Pré-requisitos
Certifique-se de ter:
* Instalado o [VS Code](https://code.visualstudio.com/).
* Instalado a extensão [PlatformIO IDE](https://platformio.org/) no VS Code.
* Clonado este repositório e estar com a pasta aberta no VS Code.

### 2. Configurar Credenciais (ESP32)

O ESP32 precisa de um arquivo de cabeçalho (`secrets.h`) com as credenciais de Wi-Fi e MQTT. Por segurança, este arquivo não é incluído no repositório.

1.  Crie um arquivo chamado **`secrets.h`** na pasta raiz do projeto (mesma pasta do `platformio.ini`).
2.  Cole o bloco de código abaixo e **altere todos os valores** para corresponderem à sua rede e ao seu broker MQTT:

    ```c
    // secrets.h

    #ifndef WIFI_SECRETS_H
    #define WIFI_SECRETS_H

    #define WIFI_SSID "NOME_DA_SUA_REDE"
    #define WIFI_PASSWORD "SUA_SENHA_WIFI"

    // Configurações do Broker MQTT
    #define MQTT_SERVER "SEU_IP_BROKER"
    #define MQTT_PORT 1883
    #define CLIENTID "ESP32Alyssa"

    // Tópicos e Autenticação (IMPORTANTE: Estes devem ser iguais aos do config.py)

    #define MQTT_TOPIC_TEMP "topico_mqtt_temp"
    #define MQTT_TOPIC_HUM "topico_mqtt_umid"
    #define MQTT_USER "user"
    #define MQTT_PASSWORD "password"

    #endif // WIFI_SECRETS_H
    ```

### 3. Compilar e Upload (ESP32)
    1. Conecte o ESP32 via USB.
    2. No PlatformIO (barra lateral esquerda ou barra inferior), clique em **Upload** (seta para a direita `→`).
    3. Aguarde a compilação e gravação.

### 4. Acessar o Painel Web
    1. Abra o **Serial Monitor** (ícone de tomada na barra inferior do PlatformIO).
    2. Reinicie o ESP32 (pressione o botão EN/RST na placa).
    3. O IP atribuído aparecerá no terminal (ex: `192.168.1.105`).
    4. Digite esse IP no navegador do seu celular ou PC para controlar os LEDs.

---

## 5. Configuração e Uso do Subscriber MQTT (Python)

Este repositório inclui um script Python (`mqtt_subscriber.py`) para monitorar os dados de Telemetria (Temperatura e Umidade) enviados pelo ESP32.

### Pré-requisitos (Python)

Instale a biblioteca `paho-mqtt`:

```bash
pip install paho-mqtt

```

###Configuração de Conexão (Python)O script Python lê as credenciais e tópicos do arquivo **`config.py`**. É crucial que estas configurações espelhem os valores definidos no `secrets.h` do ESP32 para que a comunicação funcione.

1. **Localize e Edite:** Abra o arquivo `config.py`.
2. **Exemplo de Configuração:**
```python
# config.py

# --- Configurações de Conexão ---
# Altere para o IP do seu Broker MQTT (deve ser o mesmo do secrets.h)
MQTT_BROKER = "IP_DO_SEU_BROKER" 
MQTT_PORT = 1883
CLIENT_ID = "PythonSubscriberClient"

# --- Configurações de Autenticação ---
MQTT_USER = "user" # Deve ser o mesmo do secrets.h
MQTT_PASSWORD = "password"   # Deve ser o mesmo do secrets.h

# --- Tópicos MQTT ---
# DEVEM ser IGUAIS aos definidos no secrets.h do ESP32.
MQTT_TOPIC_TEMP = "topico_mqtt_temp"
MQTT_TOPIC_HUM = "topico_mqtt_umid"

```



###Como Rodar o Subscriber1. Certifique-se de que o ESP32 está publicando dados no Broker MQTT.
2. Execute o script Python:
```bash
python mqtt_subscriber.py

```



O terminal exibirá as mensagens de Temperatura e Umidade em tempo real.

---

##Estrutura do Projeto```text
.
├── images/                  # Fotos do projeto
│   └── montagem_eletrica... # Foto da montagem física
│   └── montagem_completa... # Foto da montagem completa
│   └── teste_rainbow...     # Gif do Teste RGB Montado
├── include/                 # Headers globais
├── lib/                     # Bibliotecas privadas
├── src/                     # Código fonte principal
│   └── main.cpp             # Lógica do servidor web e controle de LEDs
├── config.py                # Configurações do MQTT Subscriber Python (Edite!)
├── secrets.h                # Credenciais Wi-Fi/MQTT do ESP32 (Crie este arquivo!)
├── platformio.ini           # Configuração do PlatformIO (Board, Baud rate)
└── README.md                # Documentação do projeto

```

##APIVocê pode integrar este projeto com automação (Home Assistant, Node-RED) usando requisições HTTP GET:

* **Ajustar Cor:** `/control?r_intensity=4095&g_intensity=0...`
* **Iniciar Efeito:** `/control?effect=rainbow&speed=30`
* **Parar Tudo:** `/control?r_intensity=0&g_intensity=0&b_intensity=0&w_intensity=0`

##Notas sobre a Montagem Física* Certifique-se de que a alimentação (Bateria/USB) é suficiente para a corrente total de todos os LEDs ligados em branco total (4095) para evitar *brownouts* (quedas de energia) no ESP32.
* **Dica:** A utilização de um capacitor eletrolítico (ex: 100uF ou 1000uF) na entrada de energia (entre VCC e GND, perto dos LEDs) é bem-vinda para estabilizar a tensão, caso a fonte sofra oscilações com a mudança brusca de brilho.