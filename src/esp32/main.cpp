#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <math.h>   // Para sin() e outras funções matemáticas
#include <stdlib.h> // Para random()
#include "secrets.h"

#include "Adafruit_TinyUSB.h"
#include "iostream/iostream.h"
#include <string>
#include <iostream>
#include <cstring>


#include <Adafruit_Sensor.h>
#include <DHT.h>

#define LEDC_TIMER_12_BIT 12
#define LEDC_BASE_FREQ 5000
#define LEDC_CHANNEL 0

/**
 * Removi a Agregacao das variaveis do secrets.h para as locais,
 * Mas Se quiserem, so sobre-escrevelas no codigo e funcionara.
 */

// Defina os GPIOs que você soldou no ESP32
#define LED_R 33
#define LED_G 32
#define LED_B 25
#define LED_W 26  // Opcional

// --- Configuração do Sensor DHT11 ---
#define DHTPIN 4       // Pino GPIO do ESP32 para o sensor DHT11
#define DHTTYPE DHT11   // Tipo de sensor (DHT11 ou DHT22)

DHT dht(DHTPIN, DHTTYPE);

// Variáveis para armazenar as leituras do DHT
volatile float currentTemperatureC = 0.0; 
volatile float currentHumidity = 0.0;
volatile unsigned long lastTempReadTime = 0;
const long readingInterval = 10000; // Leitura a cada 10 segundos (DHT11 deve ser lido no máximo a cada 2s)

// Mapeamento dos nomes dos LEDs para os pinos GPIO
const int ledPins[] = {LED_R, LED_G, LED_B, LED_W};
const char* ledNames[] = {"r", "g", "b", "w"};
const int numLeds = sizeof(ledPins) / sizeof(ledPins[0]);

// Objeto do servidor web na porta 80
WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);

// --- Variáveis Globais de Controle de Efeitos ---
volatile bool effectActive = false; 
volatile unsigned long effectStartTime = 0;
volatile float effectDuration = 0; 
volatile int effectTargetLedPin = -1; 

String currentEffectName = ""; 

// Variáveis para Efeito Strobe 
volatile unsigned long lastStrobeToggleTime = 0;
volatile int strobePeriod = 100; 
volatile bool strobeLedState = false; 
volatile int strobeTargetLedPin = -1; 

// Variáveis para Efeito Rainbow 
volatile float rainbowHue = 0.0; 
volatile int rainbowSpeed = 50; 

// Variáveis para Efeito Fire Flicker
volatile unsigned long lastFlickerTime = 0;
volatile int flickerMin = 500; 
volatile int flickerMax = 4095; 
volatile int flickerDelayMin = 50; 
volatile int flickerDelayMax = 150; 

// Variáveis para Efeito Chase
volatile unsigned long lastChaseUpdateTime = 0;
volatile int chaseDelay = 150; 
volatile int currentChaseLed = 0; 

Adafruit_USBH_CDC cdc;

template <typename T>
void CustomSerialPrint(T message) {
  if (cdc.connected()) {
    cdc.print(message);
  }
}

template <typename T>
void CustomSerialPrintln(T message) {
  if (cdc.connected()) {
    cdc.println(message);
  }
}


void reconnect() {
  // Loop até reconectar
  while (!client.connected()) {
    CustomSerialPrint("Tentando conexao MQTT...");
    // Tenta conectar, usando as credenciais, se houver
    if (client.connect(CLIENTID, MQTT_USER, MQTT_PASSWORD)) {
      CustomSerialPrintln("conectado");
      // Se desejar se inscrever em algum tópico, faça aqui
      // client.subscribe("esp32/comando/#");

      // Adicionei o IP por que meu esp e defeituoso e troca de ip toda hora
      String ip = WiFi.localIP().toString();
      client.publish(MQTT_TOPIC_IP, ip.c_str(), true); 
    } else {
      CustomSerialPrint("falhou, rc=");
      CustomSerialPrint(client.state());
      CustomSerialPrintln(" Tentando novamente em 5 segundos");
      // Espera 5 segundos antes de tentar novamente
      delay(5000);
    }
  }
}

// --- Funções Auxiliares e de Leitura ---
void setAllLeds(int r_val, int g_val, int b_val, int w_val) {
  ledcWrite(LED_R, r_val);
  ledcWrite(LED_G, g_val);
  ledcWrite(LED_B, b_val);
  if (LED_W != 0) { 
    ledcWrite(LED_W, w_val);
  }
}

// Converte HSV para RGB (Hue, Saturation, Value -> Red, Green, Blue)
int* hsvToRgb(float h, float s, float v) {
  static int rgb[3];
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
  float m = v - c;
  
  float r_prime, g_prime, b_prime;

  if (h >= 0 && h < 60) {
    r_prime = c; g_prime = x; b_prime = 0;
  } else if (h >= 60 && h < 120) {
    r_prime = x; g_prime = c; b_prime = 0;
  } else if (h >= 120 && h < 180) {
    r_prime = 0; g_prime = c; b_prime = x;
  } else if (h >= 180 && h < 240) {
    r_prime = 0; g_prime = x; b_prime = c;
  } else if (h >= 240 && h < 300) {
    r_prime = x; g_prime = 0; b_prime = c;
  } else { // h >= 300 && h < 360
    r_prime = c; g_prime = 0; b_prime = x;
  }

  rgb[0] = (int)((r_prime + m) * 4095); // Escala para 12 bits
  rgb[1] = (int)((g_prime + m) * 4095);
  rgb[2] = (int)((b_prime + m) * 4095);
  
  for(int i=0; i<3; i++) {
    if(rgb[i] < 0) rgb[i] = 0;
    if(rgb[i] > 4095) rgb[i] = 4095;
  }

  return rgb;
}

void handleNotFound() {
  if (server.uri() == "/favicon.ico") {
    server.send(204, "text/plain", "");
  } else {
    String message = "Pagina Nao Encontrada\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArgumentos: ";
    message += server.args();
    server.send(404, "text/plain", message);
  }
}

void readDhtData() {
  if (millis() - lastTempReadTime >= readingInterval) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      CustomSerialPrintln("Erro ao ler do sensor DHT!");
    } else {
      currentHumidity = h;
      currentTemperatureC = t;
      
      CustomSerialPrint("Umidade: ");
      CustomSerialPrint(currentHumidity);
      CustomSerialPrint(" %\t");
      CustomSerialPrint("Temperatura: ");
      CustomSerialPrint(currentTemperatureC);
      CustomSerialPrintln(" *C");
      
      // --- PUBLICAÇÃO MQTT ---
      String tempString = String(currentTemperatureC, 1);
      String humString = String(currentHumidity, 1);
      
      if (client.connected()) {
        // Publica a temperatura
        client.publish(MQTT_TOPIC_TEMP, tempString.c_str(), false); // false = não retido
        CustomSerialPrint("Publicado no MQTT: ");
        CustomSerialPrint(MQTT_TOPIC_TEMP);
        CustomSerialPrint(" -> ");
        CustomSerialPrintln(tempString.c_str());
        
        // Publica a umidade
        client.publish(MQTT_TOPIC_HUM, humString.c_str(), false);
        CustomSerialPrint("Publicado no MQTT: ");
        CustomSerialPrint(MQTT_TOPIC_HUM);
        CustomSerialPrint(" -> ");
        CustomSerialPrintln(humString);
      } else {
        CustomSerialPrintln("Cliente MQTT desconectado. Nao publicou.");
      }
    }
    lastTempReadTime = millis();
  }
}

void runBreathingEffect() {
  if (currentEffectName != "breathing") return; 

  unsigned long currentTime = millis();
  float elapsedSeconds = (currentTime - effectStartTime) / 1000.0;

  if (effectDuration > 0 && elapsedSeconds >= effectDuration) {
    effectActive = false;
    ledcWrite(effectTargetLedPin, 0);
    CustomSerialPrintln("Efeito breathing finalizado.");
    currentEffectName = ""; 
    return;
  }
  
  float breathingPeriod = 2.0; 
  float intensityFloat = (sin(2 * PI * elapsedSeconds / breathingPeriod) + 1) / 2.0;
  int intensity = (int)(intensityFloat * 4095);

  if (intensity < 0) intensity = 0;
  if (intensity > 4095) intensity = 4095;

  ledcWrite(effectTargetLedPin, intensity);
}

void runStrobeEffect() {
  if (currentEffectName != "strobe") return;

  unsigned long currentTime = millis();
  if (currentTime - lastStrobeToggleTime >= strobePeriod) {
    strobeLedState = !strobeLedState;
    ledcWrite(strobeTargetLedPin, strobeLedState ? 4095 : 0); 
    lastStrobeToggleTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    ledcWrite(strobeTargetLedPin, 0);
    CustomSerialPrintln("Efeito strobe finalizado.");
    currentEffectName = "";
  }
}

void runRainbowCycleEffect() {
  if (currentEffectName != "rainbow") return;

  unsigned long currentTime = millis();
  if (currentTime - lastChaseUpdateTime >= rainbowSpeed) {
    rainbowHue += 1.0; 
    if (rainbowHue >= 360.0) {
      rainbowHue = 0.0; 
    }

    int* rgb = hsvToRgb(rainbowHue, 1.0, 1.0); 
    ledcWrite(LED_R, rgb[0]);
    ledcWrite(LED_G, rgb[1]);
    ledcWrite(LED_B, rgb[2]);
    if (LED_W != 0) { 
      ledcWrite(LED_W, 0); 
    }

    lastChaseUpdateTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    setAllLeds(0,0,0,0); 
    CustomSerialPrintln("Efeito rainbow finalizado.");
    currentEffectName = "";
  }
}

void runFireFlickerEffect() {
  if (currentEffectName != "fire") return;

  unsigned long currentTime = millis();
  if (currentTime - lastFlickerTime >= random(flickerDelayMin, flickerDelayMax)) {
    int intensityR = random(flickerMin, flickerMax);
    int intensityG = random(flickerMin / 4, flickerMax / 2); 
    int intensityB = 0; 
    int intensityW = random(flickerMin / 2, flickerMax); 

    ledcWrite(LED_R, intensityR);
    ledcWrite(LED_G, intensityG);
    ledcWrite(LED_B, intensityB);
    if (LED_W != 0) {
      ledcWrite(LED_W, intensityW);
    }
    
    lastFlickerTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    setAllLeds(0,0,0,0); 
    CustomSerialPrintln("Efeito fire finalizado.");
    currentEffectName = "";
  }
}

void runChaseEffect() {
  if (currentEffectName != "chase") return;

  unsigned long currentTime = millis();
  if (currentTime - lastChaseUpdateTime >= chaseDelay) {
    setAllLeds(0,0,0,0); 

    ledcWrite(ledPins[currentChaseLed], 4095); 

    currentChaseLed = (currentChaseLed + 1) % numLeds; 

    lastChaseUpdateTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    setAllLeds(0,0,0,0); 
    CustomSerialPrintln("Efeito chase finalizado.");
    currentEffectName = "";
  }
}


// --- Funções de Manipulação HTTP ---

void handleControl() {
  if (server.hasArg("effect")) {
    String effectName = server.arg("effect");
    CustomSerialPrint("Efeito solicitado: ");
    CustomSerialPrintln(effectName);

    effectActive = false;
    currentEffectName = ""; 
    setAllLeds(0,0,0,0); 

    effectDuration = server.hasArg("duration") ? server.arg("duration").toFloat() : 0; 

    if (effectName.equalsIgnoreCase("breathing")) {
      String targetLed = server.hasArg("led") ? server.arg("led") : "r";
      effectTargetLedPin = -1; 
      for (int i = 0; i < numLeds; i++) {
        if (targetLed.equalsIgnoreCase(ledNames[i])) {
          effectTargetLedPin = ledPins[i];
          break;
        }
      }

      if (effectTargetLedPin != -1) {
        effectActive = true;
        currentEffectName = "breathing";
        effectStartTime = millis();
        String responseMsg = "Efeito breathing iniciado no LED " + targetLed;
        server.send(200, "text/plain", responseMsg);
        return;
      } else {
        server.send(400, "text/plain", "LED alvo para breathing nao encontrado.");
        return;
      }
    } else if (effectName.equalsIgnoreCase("strobe")) {
      String targetLed = server.hasArg("led") ? server.arg("led") : "r";
      effectTargetLedPin = -1; 
      for (int i = 0; i < numLeds; i++) {
        if (targetLed.equalsIgnoreCase(ledNames[i])) {
          effectTargetLedPin = ledPins[i];
          break;
        }
      }

      if (effectTargetLedPin != -1) {
        effectActive = true;
        currentEffectName = "strobe";
        strobeTargetLedPin = effectTargetLedPin; 
        strobePeriod = server.hasArg("period") ? server.arg("period").toInt() : 100; 
        lastStrobeToggleTime = millis();
        strobeLedState = true;
        
        String responseMsg = "Efeito strobe iniciado no LED " + targetLed;
        server.send(200, "text/plain", responseMsg);
        return;
      } else {
        server.send(400, "text/plain", "LED alvo para strobe nao encontrado.");
        return;
      }
    } else if (effectName.equalsIgnoreCase("rainbow")) {
        effectActive = true;
        currentEffectName = "rainbow";
        effectStartTime = millis();
        rainbowHue = 0.0; 
        rainbowSpeed = server.hasArg("speed") ? server.arg("speed").toInt() : 50; 
        server.send(200, "text/plain", "Efeito rainbow iniciado.");
        return;
    } else if (effectName.equalsIgnoreCase("fire")) {
        effectActive = true;
        currentEffectName = "fire";
        effectStartTime = millis();
        randomSeed(analogRead(0)); 
        lastFlickerTime = millis();
        server.send(200, "text/plain", "Efeito fire iniciado.");
        return;
    } else if (effectName.equalsIgnoreCase("chase")) {
        effectActive = true;
        currentEffectName = "chase";
        effectStartTime = millis();
        chaseDelay = server.hasArg("delay") ? server.arg("delay").toInt() : 150; 
        currentChaseLed = 0; 
        setAllLeds(0,0,0,0); 
        server.send(200, "text/plain", "Efeito chase iniciado.");
        return;
    }
    else {
      server.send(400, "text/plain", "Efeito desconhecido.");
      return;
    }
  }

  effectActive = false; 
  currentEffectName = ""; 
  
  bool ledControlled = false;
  for (int i = 0; i < numLeds; i++) {
    String paramName = String(ledNames[i]) + "_intensity";
    if (server.hasArg(paramName)) {
      int intensity = server.arg(paramName).toInt();
      if (intensity < 0) intensity = 0;
      if (intensity > 4095) intensity = 4095;
      ledcWrite(ledPins[i], intensity);
      ledControlled = true;
    }
  }

  if (ledControlled) {
    server.send(200, "text/plain", "LEDs ajustados.");
  } else {
    server.send(400, "text/plain", "Nenhum parametro de LED ou efeito valido fornecido.");
  }
}

void handleTemperature() {
  String tempResponse = String(currentTemperatureC, 1); 
  server.send(200, "text/plain", tempResponse);
}

void handleHumidity() {
  String humResponse = String(currentHumidity, 1); 
  server.send(200, "text/plain", humResponse);
}

// Função para lidar com a requisição da página inicial (HTML)
void handleRoot() {
  String html = "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<title>Controle de LEDs ESP32</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #f0f0f0; }";
  html += "h1 { color: #333; }";
  html += "div { margin-bottom: 20px; padding: 15px; border-radius: 8px; background-color: #fff; box-shadow: 0 2px 4px rgba(0,0,0,0.1); display: inline-block; }";
  html += "input[type=range] { width: 300px; margin: 10px 0; }";
  html += "label { font-weight: bold; margin-right: 10px; }";
  html += "button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 10px; }";
  html += "button:hover { background-color: #45a049; }";
  html += ".effect-btn { background-color: #008CBA; margin: 5px; }"; 
  html += ".effect-btn:hover { background-color: #007bb5; }";
  html += ".stop-btn { background-color: #f44336; margin: 5px; }"; 
  html += ".stop-btn:hover { background-color: #da190b; }";
  html += "p { margin: 5px 0; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<h1>Controle de LEDs RGBW</h1>";

  html += "<h2>Monitoramento DHT11</h2>";
  html += "<div>";
  html += "  <p>Temperatura: <span id='currentTemp'>--</span> &deg;C</p>";
  html += "  <p>Umidade: <span id='currentHumidity'>--</span> %</p>";
  html += "</div>";

  html += "<h2>Controle Individual</h2>";
  for (int i = 0; i < numLeds; i++) {
    String ledNameUpper = String(ledNames[i]);
    ledNameUpper.toUpperCase();
    html += "<div>";
    html += "<label for='" + String(ledNames[i]) + "'>LED " + ledNameUpper + ":</label>";
    html += "<input type='range' id='" + String(ledNames[i]) + "' min='0' max='4095' value='0' oninput='updateLed(\"" + String(ledNames[i]) + "\", this.value)'>";
    html += "<span id='value" + String(ledNames[i]) + "'>0</span>";
    html += "</div><br>";
  }
  
  html += "<h2>Efeitos</h2>";
  html += "<div>";
  html += "<h3>Breathing</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"breathing\", \"r\")'>Vermelho</button>";
  html += "<button class='effect-btn' onclick='startEffect(\"breathing\", \"g\")'>Verde</button>";
  html += "<button class='effect-btn' onclick='startEffect(\"breathing\", \"b\")'>Azul</button>";
  html += "<button class='effect-btn' onclick='startEffect(\"breathing\", \"w\")'>Branco</button>";
  html += "</div>";

  html += "<div>";
  html += "<h3>Strobe</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"strobe\", \"r\", 0, 50)'>Vermelho (Rápido)</button>"; 
  html += "<button class='effect-btn' onclick='startEffect(\"strobe\", \"g\", 0, 200)'>Verde (Lento)</button>"; 
  html += "</div>";
  
  html += "<div>";
  html += "<h3>Rainbow Cycle</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"rainbow\", \"\", 0, 30)'>Iniciar Rainbow</button>"; 
  html += "</div>";

  html += "<div>";
  html += "<h3>Fire Flicker</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"fire\")'>Iniciar Fogo</button>"; 
  html += "</div>";

  html += "<div>";
  html += "<h3>Chase</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"chase\", \"\", 0, 100)'>Iniciar Perseguição</button>"; 
  html += "</div>";

  html += "<div>";
  html += "<button class='stop-btn' onclick='stopEffects()'>Parar Todos os Efeitos e Desligar LEDs</button>";
  html += "</div>";


  html += "<script>";
  html += "function updateLed(led, intensity) {";
  html += "  document.getElementById('value' + led).innerText = intensity;";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/control?' + led + '_intensity=' + intensity, true);"; 
  html += "  xhr.send();";
  html += "}";

  html += "function startEffect(effectName, ledTarget = '', duration = 0, extraParam = 0) {"; 
  html += "  var xhr = new XMLHttpRequest();";
  html += "  var url = '/control?effect=' + effectName;";
  html += "  if (ledTarget) url += '&led=' + ledTarget;";
  html += "  if (duration > 0) url += '&duration=' + duration;";
  
  html += "  if (extraParam > 0) {";
  html += "    if (effectName === 'strobe') {";
  html += "      url += '&period=' + extraParam;"; 
  html += "    } else if (effectName === 'chase') {";
  html += "      url += '&delay=' + extraParam;";
  html += "    } else if (effectName === 'rainbow') {";
  html += "      url += '&speed=' + extraParam;";
  html += "    }";
  html += "  }";

  html += "  xhr.open('GET', url, true);";
  html += "  xhr.send();";
  html += "  console.log('Efeito ' + effectName + ' iniciado.');";
  html += "}"; 

  html += "function stopEffects() {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/control?r_intensity=0&g_intensity=0&b_intensity=0&w_intensity=0', true);"; 
  html += "  xhr.send();";
  html += "  console.log('Efeitos parados e LEDs desligados.');"; 
  html += "}";

  html += "function fetchDhtData() {";
  html += "  // Busca Temperatura";
  html += "  var xhrTemp = new XMLHttpRequest();";
  html += "  xhrTemp.onreadystatechange = function() {";
  html += "    if (xhrTemp.readyState == 4 && xhrTemp.status == 200) {";
  html += "      document.getElementById('currentTemp').innerText = xhrTemp.responseText;";
  html += "    }";
  html += "  };";
  html += "  xhrTemp.open('GET', '/temp', true);"; 
  html += "  xhrTemp.send();";

  html += "  // Busca Umidade";
  html += "  var xhrHum = new XMLHttpRequest();";
  html += "  xhrHum.onreadystatechange = function() {";
  html += "    if (xhrHum.readyState == 4 && xhrHum.status == 200) {";
  html += "      document.getElementById('currentHumidity').innerText = xhrHum.responseText;";
  html += "    }";
  html += "  };";
  html += "  xhrHum.open('GET', '/humidity', true);"; 
  html += "  xhrHum.send();";
  html += "}";

  // Chama a função a cada 5 segundos
  html += "setInterval(fetchDhtData, 5000);"; 
  // Chama imediatamente ao carregar
  html += "window.onload = function() { fetchDhtData(); };";

  html += "</script>";
  html += "</body>";
  html += "</html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);

  // Inicia os canais PWM para os LEDs
  for (int i = 0; i < numLeds; i++) {
    ledcAttachPin(ledPins[i], LEDC_CHANNEL);
    ledcWrite(ledPins[i], 0); // Desliga todos os LEDs no início
  }
  
  // --- Inicializa o Sensor DHT11 ---
  dht.begin(); 

  CustomSerialPrint("Conectando-se ao Wi-Fi ");
  CustomSerialPrintln(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    CustomSerialPrint(".");
  }

  CustomSerialPrintln("");
  CustomSerialPrintln("WiFi conectado!");
  CustomSerialPrint("Endereço IP: ");
  CustomSerialPrintln(WiFi.localIP());

  // --- CONFIGURAÇÃO MQTT ---
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // Configura as rotas do servidor
  server.on("/", handleRoot);
  server.on("/control", handleControl); 
  server.on("/temp", handleTemperature);   // Endpoint para Temperatura
  server.on("/humidity", handleHumidity); // Endpoint para Umidade

  // Inicia o servidor
  server.begin();
  CustomSerialPrintln("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient(); // Lida com as requisições HTTP

  // --- NOVO: Lida com a conexão MQTT ---
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Processa mensagens de entrada e mantém o keep-alive

  // Executa o efeito ativo, se houver
  if (effectActive) {
    if (currentEffectName == "breathing") {
      runBreathingEffect();
    } else if (currentEffectName == "strobe") {
      runStrobeEffect();
    } else if (currentEffectName == "rainbow") {
      runRainbowCycleEffect();
    } else if (currentEffectName == "fire") {
      runFireFlickerEffect();
    } else if (currentEffectName == "chase") {
      runChaseEffect();
    }
  }
  readDhtData(); 
}