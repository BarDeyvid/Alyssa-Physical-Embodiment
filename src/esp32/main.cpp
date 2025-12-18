#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <vector>
#include "secrets.h"
#include "web_index.h" 

// --- Definições de Hardware ---
#define LED_R 33
#define LED_G 32
#define LED_B 25
#define LED_W 26
#define DHTPIN 4
#define DHTTYPE DHT11

// Configuração PWM
const int PWM_FREQ = 5000;
const int PWM_RES = 12;
const int MAX_DUTY = 4095;

// Mapeamento de Pinos
const std::vector<int> ledPins = {LED_R, LED_G, LED_B, LED_W};
// Mapeamento lógico R=0, G=1, B=2, W=3

// --- Objetos Globais ---
DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Estado do Sistema ---
struct SystemState {
    float temp = 0.0;
    float hum = 0.0;
    int currentPWM[4] = {0, 0, 0, 0}; // Estado atual dos LEDs
    
    // Controle de Efeitos
    bool effectActive = false;
    String currentEffect = "";
    unsigned long lastEffectStep = 0;
    int effectStepIndex = 0;
    int effectSpeed = 50; 
    int effectTargetCh = -1; // -1 = Todos
    
    // Auxiliares para efeitos
    float hue = 0.0;
    bool direction = true;
};

SystemState sysState;

// Timers Não Bloqueantes
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 10000;
unsigned long lastMqttRetry = 0;

// --- Protótipos ---
void setLedRaw(int channel, int duty);
void setAllLeds(int r, int g, int b, int w);
void handleEffects();

// --- Funções Auxiliares ---

// Converte HSV para RGB (Ponteiros para eficiência)
void hsv2rgb(float h, float s, float v, int &r, int &g, int &b) {
    float c = v * s;
    float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
    float m = v - c;
    float rp, gp, bp;
    
    if (h < 60) { rp = c; gp = x; bp = 0; }
    else if (h < 120) { rp = x; gp = c; bp = 0; }
    else if (h < 180) { rp = 0; gp = c; bp = x; }
    else if (h < 240) { rp = 0; gp = x; bp = c; }
    else if (h < 300) { rp = x; gp = 0; bp = c; }
    else { rp = c; gp = 0; bp = x; }
    
    r = (rp + m) * MAX_DUTY;
    g = (gp + m) * MAX_DUTY;
    b = (bp + m) * MAX_DUTY;
}

void setLedRaw(int channel, int duty) {
    if (channel < 0 || channel > 3) return;
    ledcWrite(channel, duty);
    sysState.currentPWM[channel] = duty;
}

void setAllLeds(int r, int g, int b, int w) {
    setLedRaw(0, r); setLedRaw(1, g); setLedRaw(2, b); setLedRaw(3, w);
}

// --- Efeitos ---

void runRainbow() {
    int r, g, b;
    hsv2rgb(sysState.hue, 1.0, 1.0, r, g, b);
    setAllLeds(r, g, b, 0);
    sysState.hue += 1.0;
    if (sysState.hue >= 360) sysState.hue = 0;
}

void runFire() {
    // Simulação de fogo cintilante (Red + pouco Green + pouco White)
    int r = random(MAX_DUTY * 0.6, MAX_DUTY);
    int g = random(MAX_DUTY * 0.1, MAX_DUTY * 0.2); 
    int w = random(0, MAX_DUTY * 0.05);
    setAllLeds(r, g, 0, w);
}

void runChase() {
    for(int i=0; i<4; i++) {
        setLedRaw(i, (i == sysState.effectStepIndex) ? MAX_DUTY : 0);
    }
    sysState.effectStepIndex = (sysState.effectStepIndex + 1) % 4;
}

void runAurora() {
    // Transição suave de cores pastel (Ciano -> Roxo -> Azul)
    // Usa uma função seno para suavidade
    float t = millis() / 1000.0;
    int r = (sin(t) + 1) * 2000;
    int g = (sin(t + 2) + 1) * 2000;
    int b = (sin(t + 4) + 1) * 2000;
    setAllLeds(r, g, b, 500); // Fundo branco suave
}

void runPolice() {
    // Strobe Vermelho e Azul alternado
    if (sysState.effectStepIndex % 2 == 0) setAllLeds(MAX_DUTY, 0, 0, 0);
    else setAllLeds(0, 0, MAX_DUTY, 0);
    sysState.effectStepIndex++;
}

void runPulse() {
    // Pulsa um canal específico ou todos
    float val = (exp(sin(millis()/2000.0*PI)) - 0.367879441) * 108.0; // Math exp para pulso "cardíaco"
    int intensity = map((int)val, 0, 255, 0, MAX_DUTY);
    
    if (sysState.effectTargetCh >= 0) {
        setAllLeds(0,0,0,0); // Apaga outros
        setLedRaw(sysState.effectTargetCh, intensity);
    } else {
        setAllLeds(intensity, 0, 0, 0); // Padrão vermelho se nenhum selecionado
    }
}

void handleEffects() {
    if (!sysState.effectActive) return;

    unsigned long now = millis();
    // Velocidade dinâmica baseada no efeito
    int speed = sysState.effectSpeed;
    if (sysState.currentEffect == "aurora") speed = 20; // Mais fluido
    if (sysState.currentEffect == "pulse") speed = 10;
    
    if (now - sysState.lastEffectStep >= speed) {
        if (sysState.currentEffect == "rainbow") runRainbow();
        else if (sysState.currentEffect == "fire") runFire();
        else if (sysState.currentEffect == "chase") runChase();
        else if (sysState.currentEffect == "aurora") runAurora();
        else if (sysState.currentEffect == "police") runPolice();
        else if (sysState.currentEffect == "pulse") runPulse();
        
        sysState.lastEffectStep = now;
    }
}

// --- Setup Servidor Web ---

void setupServer() {
    // Rota Principal
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    // API Status (JSON)
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        char json[200];
        snprintf(json, sizeof(json), 
            "{\"temp\":%.1f,\"hum\":%.1f,\"effectActive\":%s,\"pwm\":[%d,%d,%d,%d]}",
            sysState.temp, sysState.hum, 
            sysState.effectActive ? "true" : "false",
            sysState.currentPWM[0], sysState.currentPWM[1], sysState.currentPWM[2], sysState.currentPWM[3]
        );
        request->send(200, "application/json", json);
    });

    // API Controle (Recebe JSON)
    AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/api/control", 
    [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        
        String mode = jsonObj["mode"] | "stop";

        if (mode == "stop") {
            sysState.effectActive = false;
            setAllLeds(0,0,0,0);
        }
        else if (mode == "manual") {
            sysState.effectActive = false;
            String chStr = jsonObj["channel"] | "r";
            int val = jsonObj["value"] | 0;
            
            if (chStr == "r") setLedRaw(0, val);
            else if (chStr == "g") setLedRaw(1, val);
            else if (chStr == "b") setLedRaw(2, val);
            else if (chStr == "w") setLedRaw(3, val);
        }
        else if (mode == "effect") {
            sysState.effectActive = true;
            sysState.currentEffect = jsonObj["effect"].as<String>();
            sysState.effectSpeed = jsonObj["speed"] | 100; // Padrão 100ms
            
            // Configurações específicas
            if (sysState.currentEffect == "police") sysState.effectSpeed = 150;
            if (sysState.currentEffect == "fire") sysState.effectSpeed = 80;
            
            String target = jsonObj["led"] | "";
            sysState.effectTargetCh = -1;
            if (target == "r") sysState.effectTargetCh = 0;
            else if (target == "g") sysState.effectTargetCh = 1;
            else if (target == "b") sysState.effectTargetCh = 2;
            else if (target == "w") sysState.effectTargetCh = 3;
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.addHandler(handler);
    server.begin();
}

// --- Setup e Loop ---

void setup() {
    Serial.begin(115200);
    
    // Configura PWM
    for (int i = 0; i < 4; i++) {
        ledcSetup(i, PWM_FREQ, PWM_RES);
        ledcAttachPin(ledPins[i], i);
        ledcWrite(i, 0);
    }

    dht.begin();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Conectado IP: " + WiFi.localIP().toString());

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    
    setupServer();
}

void loop() {
    // 1. MQTT (Reconexão não bloqueante)
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastMqttRetry > 15000) {
            lastMqttRetry = now;
            if (mqttClient.connect(CLIENTID, MQTT_USER, MQTT_PASSWORD)) {
                Serial.println("MQTT Reconectado");
                mqttClient.publish(MQTT_TOPIC_IP, WiFi.localIP().toString().c_str(), true);
            }
        }
    } else {
        mqttClient.loop();
    }

    // 2. Leitura Sensores (Intervalo Fixo)
    if (millis() - lastSensorRead > SENSOR_INTERVAL) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        
        if (!isnan(t) && !isnan(h)) {
            sysState.temp = t;
            sysState.hum = h;
            
            if (mqttClient.connected()) {
                char tmpStr[8];
                dtostrf(t, 4, 1, tmpStr);
                mqttClient.publish(MQTT_TOPIC_TEMP, tmpStr);
                
                dtostrf(h, 4, 1, tmpStr);
                mqttClient.publish(MQTT_TOPIC_HUM, tmpStr);
            }
        }
        lastSensorRead = millis();
    }

    // 3. Execução de Efeitos
    handleEffects();
}