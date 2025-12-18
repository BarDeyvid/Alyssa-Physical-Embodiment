#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <math.h>
#include <stdlib.h>
#include "secrets.h"
#include "Adafruit_TinyUSB.h"
#include <Adafruit_Sensor.h>
#include <DHT.h>

// --- Configurações de Hardware ---
#define LED_R 33
#define LED_G 32
#define LED_B 25
#define LED_W 26
#define DHTPIN 4
#define DHTTYPE DHT11

#define LEDC_TIMER_12_BIT 12
#define LEDC_BASE_FREQ 5000

// --- Objetos e Variáveis Globais ---
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_USBH_CDC cdc;

const int ledPins[] = {LED_R, LED_G, LED_B, LED_W};
const char* ledNames[] = {"r", "g", "b", "w"};
const int numLeds = sizeof(ledPins) / sizeof(ledPins[0]);

// Estado dos LEDs e Transição (Fade)
int targetIntensity[4] = {0, 0, 0, 0};
float currentIntensity[4] = {0, 0, 0, 0};
float fadeSpeed = 0.1; 

// Sensores
volatile float currentTemperatureC = 0.0; 
volatile float currentHumidity = 0.0;
unsigned long lastTempReadTime = 0;
const long readingInterval = 10000;

// Efeitos
volatile bool effectActive = false;
String currentEffectName = "";
unsigned long effectStartTime = 0;
float effectDuration = 0;
int effectTargetLedPin = -1; // Usado para efeitos que miram um pino específico (breathing/strobe)
int effectTargetChannel = -1; // Mapeado para o canal PWM (0-3)

// Variáveis auxiliares de efeitos
unsigned long lastEffectUpdate = 0;
int strobePeriod = 100;
bool strobeState = false;
float rainbowHue = 0.0;
int rainbowSpeed = 50;
int flickerMin = 500;
int flickerMax = 4095;
int chaseDelay = 150;
int chaseIndex = 0;

// MQTT Não Bloqueante
unsigned long lastMqttRetry = 0;
const unsigned long mqttRetryInterval = 15000;

// --- Sistema de Logs com Templates ---
enum LogLevel { INFO, WARN, ERROR };

template <typename T>
void CustomLog(LogLevel level, T message) {
  if (!cdc.connected()) return;
  if (level == INFO) cdc.print("[INFO] ");
  else if (level == WARN) cdc.print("[WARN] ");
  else cdc.print("[ERRO] ");
  cdc.println(message);
}

// --- Funções Auxiliares de LED ---

void setAllLedsInstant(int r, int g, int b, int w) {
  ledcWrite(0, r); ledcWrite(1, g); ledcWrite(2, b); ledcWrite(3, w);
  for(int i=0; i<4; i++) currentIntensity[i] = targetIntensity[i] = (i==0?r:i==1?g:i==2?b:w);
}

void updateLedsFade() {
  if (effectActive) return; 
  for (int i = 0; i < numLeds; i++) {
    if (abs(currentIntensity[i] - targetIntensity[i]) > 0.5) {
      currentIntensity[i] += (targetIntensity[i] - currentIntensity[i]) * fadeSpeed;
      ledcWrite(i, (int)currentIntensity[i]);
    }
  }
}

int* hsvToRgb(float h, float s, float v) {
  static int rgb[3];
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
  float m = v - c;
  float r_prime, g_prime, b_prime;
  if (h < 60) { r_prime = c; g_prime = x; b_prime = 0; }
  else if (h < 120) { r_prime = x; g_prime = c; b_prime = 0; }
  else if (h < 180) { r_prime = 0; g_prime = c; b_prime = x; }
  else if (h < 240) { r_prime = 0; g_prime = x; b_prime = c; }
  else if (h < 300) { r_prime = x; g_prime = 0; b_prime = c; }
  else { r_prime = c; g_prime = 0; b_prime = x; }
  rgb[0] = (int)((r_prime + m) * 4095);
  rgb[1] = (int)((g_prime + m) * 4095);
  rgb[2] = (int)((b_prime + m) * 4095);
  return rgb;
}

// --- Lógica de Sensores e MQTT ---

void handleMqtt() {
  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastMqttRetry > mqttRetryInterval) {
      lastMqttRetry = now;
      if (client.connect(CLIENTID, MQTT_USER, MQTT_PASSWORD)) {
        CustomLog(INFO, "MQTT Conectado!");
        client.publish(MQTT_TOPIC_IP, WiFi.localIP().toString().c_str(), true);
      }
    }
  } else { client.loop(); }
}

void readDhtData() {
  if (millis() - lastTempReadTime >= readingInterval) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      currentHumidity = h; currentTemperatureC = t;
      if (client.connected()) {
        client.publish(MQTT_TOPIC_TEMP, String(t, 1).c_str());
        client.publish(MQTT_TOPIC_HUM, String(h, 1).c_str());
      }
    }
    lastTempReadTime = millis();
  }
}

// --- Handlers Web ---

void handleControl() {
  if (server.hasArg("effect")) {
    String effectName = server.arg("effect");
    CustomLog(INFO, "Efeito solicitado: " + effectName);

    effectActive = false; 
    currentEffectName = "";
    setAllLedsInstant(0,0,0,0);

    effectDuration = server.hasArg("duration") ? server.arg("duration").toFloat() : 0;

    if (effectName.equalsIgnoreCase("breathing") || effectName.equalsIgnoreCase("strobe")) {
      String targetLed = server.hasArg("led") ? server.arg("led") : "r";
      effectTargetChannel = -1;
      for (int i = 0; i < numLeds; i++) {
        if (targetLed.equalsIgnoreCase(ledNames[i])) { effectTargetChannel = i; break; }
      }
      if (effectTargetChannel != -1) {
        effectActive = true;
        currentEffectName = effectName;
        effectStartTime = millis();
        if(effectName.equalsIgnoreCase("strobe")) {
          strobePeriod = server.hasArg("period") ? server.arg("period").toInt() : 100;
        }
        server.send(200, "text/plain", "Efeito " + effectName + " iniciado no LED " + targetLed);
        return;
      }
    } else if (effectName.equalsIgnoreCase("rainbow")) {
      effectActive = true; currentEffectName = "rainbow";
      rainbowSpeed = server.hasArg("speed") ? server.arg("speed").toInt() : 50;
      rainbowHue = 0; effectStartTime = millis();
      server.send(200, "text/plain", "Rainbow iniciado"); return;
    } else if (effectName.equalsIgnoreCase("fire")) {
      effectActive = true; currentEffectName = "fire";
      effectStartTime = millis(); randomSeed(analogRead(0));
      server.send(200, "text/plain", "Fire iniciado"); return;
    } else if (effectName.equalsIgnoreCase("chase")) {
      effectActive = true; currentEffectName = "chase";
      chaseDelay = server.hasArg("delay") ? server.arg("delay").toInt() : 150;
      chaseIndex = 0; effectStartTime = millis();
      server.send(200, "text/plain", "Chase iniciado"); return;
    }
  }

  // Controle Manual (Sliders)
  effectActive = false;
  bool controlled = false;
  for (int i = 0; i < numLeds; i++) {
    String param = String(ledNames[i]) + "_intensity";
    if (server.hasArg(param)) {
      targetIntensity[i] = constrain(server.arg(param).toInt(), 0, 4095);
      controlled = true;
    }
  }
  server.send(200, "text/plain", controlled ? "Ajustado" : "Erro");
}

void handleTemperature() { server.send(200, "text/plain", String(currentTemperatureC, 1)); }
void handleHumidity() { server.send(200, "text/plain", String(currentHumidity, 1)); }

// --- Efeitos Rodando no Loop ---

void runEffects() {
  if (!effectActive) return;
  unsigned long now = millis();

  if (currentEffectName == "breathing") {
    float val = (sin(2 * PI * (now - effectStartTime) / 2000.0) + 1) / 2.0;
    ledcWrite(effectTargetChannel, (int)(val * 4095));
  } 
  else if (currentEffectName == "strobe") {
    if (now - lastEffectUpdate >= strobePeriod) {
      strobeState = !strobeState;
      ledcWrite(effectTargetChannel, strobeState ? 4095 : 0);
      lastEffectUpdate = now;
    }
  } 
  else if (currentEffectName == "rainbow") {
    if (now - lastEffectUpdate >= rainbowSpeed) {
      rainbowHue = fmod(rainbowHue + 1, 360);
      int* rgb = hsvToRgb(rainbowHue, 1.0, 1.0);
      ledcWrite(0, rgb[0]); ledcWrite(1, rgb[1]); ledcWrite(2, rgb[2]); ledcWrite(3, 0);
      lastEffectUpdate = now;
    }
  }
  else if (currentEffectName == "fire") {
    if (now - lastEffectUpdate >= random(50, 150)) {
      ledcWrite(0, random(500, 4095)); // R
      ledcWrite(1, random(100, 1000)); // G
      ledcWrite(3, random(200, 2000)); // W
      lastEffectUpdate = now;
    }
  }
  else if (currentEffectName == "chase") {
    if (now - lastEffectUpdate >= chaseDelay) {
      for(int i=0; i<4; i++) ledcWrite(i, (i == chaseIndex) ? 4095 : 0);
      chaseIndex = (chaseIndex + 1) % numLeds;
      lastEffectUpdate = now;
    }
  }

  if (effectDuration > 0 && (now - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false; setAllLedsInstant(0,0,0,0);
  }
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
html += " <p>Temperatura: <span id='currentTemp'>--</span> &deg;C</p>";
html += " <p>Umidade: <span id='currentHumidity'>--</span> %</p>";
html += "</div>";
html += "<h2>Controle Individual</h2>";
for (int i = 0; i < numLeds; i++) {
String ledNameUpper = String(ledNames[i]);
ledNameUpper.toUpperCase();
html += "<div>";
html += "<label for='" + String(ledNames[i]) + "'>LED " + ledNameUpper + ":</label>";
html += "<input type='range' id='" + String(ledNames[i]) + "' min='0' max='4095' value='0' oninput='updateLed(\"" + String(ledNames[i]) + "\", this.value)'>";
html += "<span id='value" + String(ledNames[i]) + "'>0</span>";
html += "</div><br>";}
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
html += " document.getElementById('value' + led).innerText = intensity;";
html += " var xhr = new XMLHttpRequest();";
html += " xhr.open('GET', '/control?' + led + '_intensity=' + intensity, true);";
html += " xhr.send();";
html += "}";
html += "function startEffect(effectName, ledTarget = '', duration = 0, extraParam = 0) {";
html += " var xhr = new XMLHttpRequest();";
html += " var url = '/control?effect=' + effectName;";
html += " if (ledTarget) url += '&led=' + ledTarget;";
html += " if (duration > 0) url += '&duration=' + duration;";
html += " if (extraParam > 0) {";
html += " if (effectName === 'strobe') {";
html += " url += '&period=' + extraParam;";
html += " } else if (effectName === 'chase') {";
html += " url += '&delay=' + extraParam;";
html += " } else if (effectName === 'rainbow') {";
html += " url += '&speed=' + extraParam;";
html += " }";
html += " }";
html += " xhr.open('GET', url, true);";
html += " xhr.send();";
html += " console.log('Efeito ' + effectName + ' iniciado.');";
html += "}";
html += "function stopEffects() {";
html += " var xhr = new XMLHttpRequest();";
html += " xhr.open('GET', '/control?r_intensity=0&g_intensity=0&b_intensity=0&w_intensity=0', true);";
html += " xhr.send();";
html += " console.log('Efeitos parados e LEDs desligados.');";
html += "}";
html += "function fetchDhtData() {";
html += " // Busca Temperatura";
html += " var xhrTemp = new XMLHttpRequest();";
html += " xhrTemp.onreadystatechange = function() {";
html += " if (xhrTemp.readyState == 4 && xhrTemp.status == 200) {";
html += " document.getElementById('currentTemp').innerText = xhrTemp.responseText;";
html += " }";
html += " };";
html += " xhrTemp.open('GET', '/temp', true);";
html += " xhrTemp.send();";
html += " // Busca Umidade";
html += " var xhrHum = new XMLHttpRequest();";
html += " xhrHum.onreadystatechange = function() {";
html += " if (xhrHum.readyState == 4 && xhrHum.status == 200) {";
html += " document.getElementById('currentHumidity').innerText = xhrHum.responseText;";
html += " }";
html += " };";
html += " xhrHum.open('GET', '/humidity', true);";
html += " xhrHum.send();";
html += "}";
html += "setInterval(fetchDhtData, 5000);";
html += "window.onload = function() { fetchDhtData(); };";
html += "</script>";
html += "</body>";
html += "</html>";
server.send(200, "text/html", html);
} 

// --- Setup e Loop ---

void setup() {
  setCpuFrequencyMhz(160);
  dht.begin();
  for (int i = 0; i < numLeds; i++) {
    ledcSetup(i, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
    ledcAttachPin(ledPins[i], i);
    ledcWrite(i, 0);
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  client.setServer(MQTT_SERVER, MQTT_PORT);
  
  // Handlers
  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/temp", handleTemperature);
  server.on("/humidity", handleHumidity);
  server.begin();
}

void loop() {
  server.handleClient();
  handleMqtt();
  readDhtData();
  if (effectActive) runEffects(); else updateLedsFade();
  delay(1); 
}