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
#define FAN_PIN 27   // Fio Azul (PWM)
#define TACH_PIN 14  // Fio Amarelo/Verde (Sensor Hall)
#define DHTPIN 4
#define DHTTYPE DHT11

// Configuração PWM
const int PWM_RES = 12;      // Resolução 12-bit (0-4095)
const int LED_FREQ = 5000;   // 5kHz para LEDs
const int FAN_FREQ = 25000;  // 25kHz padrão Intel para Fans
const int MAX_DUTY = 4095;

// Canal PWM dedicado para a Fan (0-3 usados pelos LEDs)
const int FAN_CHANNEL = 4;

// Mapeamento de Pinos
const std::vector<int> ledPins = {LED_R, LED_G, LED_B, LED_W};

// --- Objetos Globais ---
DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Interrupção Tacômetro
volatile int tachPulses = 0;
void IRAM_ATTR tachISR() {
    tachPulses++;
}

// --- Estado do Sistema ---
struct SystemState {
    float temp = 0.0;
    float hum = 0.0;
    int currentPWM[4] = {0, 0, 0, 0}; 
    bool effectActive = false;
    String currentEffect = "";
    int effectStepIndex = 0;
    unsigned long lastEffectStep = 0;
    int effectSpeed = 50; 
    int effectTargetCh = -1;
    float hue = 0.0;
    bool fanAutoMode = true;
    int fanPWM = 0;
    int fanRPM = 0;
};

SystemState sysState;

// Timers
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 10000; // 10s para leitura DHT
unsigned long lastMqttRetry = 0;
unsigned long lastFanUpdate = 0;
const unsigned long FAN_INTERVAL = 1000; // 1s para RPM

// --- Funções Auxiliares ---

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

void setFanSpeed(int duty) {
    if (duty > MAX_DUTY) duty = MAX_DUTY;
    if (duty < 0) duty = 0;
    ledcWrite(FAN_CHANNEL, duty);
    sysState.fanPWM = duty;
}

// --- Lógica de Hardware ---

// [CORREÇÃO] Função que estava faltando
void updateSensors() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    // Validar leitura
    if (!isnan(t)) sysState.temp = t;
    if (!isnan(h)) sysState.hum = h;

    if (mqttClient.connected() && !isnan(t)) {
        mqttClient.publish(MQTT_TOPIC_TEMP, String(sysState.temp, 1).c_str());
        mqttClient.publish(MQTT_TOPIC_HUM, String(sysState.hum, 1).c_str());
    }
}

void updateFanLogic() {
    // 1. Calcular RPM
    noInterrupts();
    int pulses = tachPulses;
    tachPulses = 0;
    interrupts();
    sysState.fanRPM = (pulses / 2) * 60;

    // 2. Lógica Automática
    if (sysState.fanAutoMode) {
        int targetPWM = 0;
        if (sysState.temp >= 50.0) {
            targetPWM = MAX_DUTY; // 100%
        } else if (sysState.temp >= 40.0) {
            targetPWM = (int)(MAX_DUTY * 0.40); // 40%
        } else {
            targetPWM = 0; // Silent Mode
        }
        setFanSpeed(targetPWM);
    }
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
    float t = millis() / 1000.0;
    int r = (sin(t) + 1) * 2000;
    int g = (sin(t + 2) + 1) * 2000;
    int b = (sin(t + 4) + 1) * 2000;
    setAllLeds(r, g, b, 500); 
}

void runPolice() {
    if (sysState.effectStepIndex % 2 == 0) setAllLeds(MAX_DUTY, 0, 0, 0);
    else setAllLeds(0, 0, MAX_DUTY, 0);
    sysState.effectStepIndex++;
}

void runPulse() {
    float val = (exp(sin(millis()/2000.0*PI)) - 0.367879441) * 108.0;
    int intensity = map((int)val, 0, 255, 0, MAX_DUTY);
    if (sysState.effectTargetCh >= 0) {
        setAllLeds(0,0,0,0);
        setLedRaw(sysState.effectTargetCh, intensity);
    } else {
        setAllLeds(intensity, 0, 0, 0);
    }
}

void handleEffects() {
    if (!sysState.effectActive) return;
    unsigned long now = millis();
    int speed = sysState.effectSpeed;
    if (sysState.currentEffect == "aurora") speed = 20;
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
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"temp\":" + String(sysState.temp, 1) + ",";
        json += "\"hum\":" + String(sysState.hum, 1) + ",";
        json += "\"fanAuto\":" + String(sysState.fanAutoMode ? "true" : "false") + ",";
        json += "\"fanPWM\":" + String(sysState.fanPWM) + ",";
        json += "\"rpm\":" + String(sysState.fanRPM) + ",";
        json += "\"effectActive\":" + String(sysState.effectActive ? "true" : "false") + ",";
        json += "\"pwm\":[" + String(sysState.currentPWM[0]) + "," + 
                              String(sysState.currentPWM[1]) + "," + 
                              String(sysState.currentPWM[2]) + "," + 
                              String(sysState.currentPWM[3]) + "]";
        json += "}";
        request->send(200, "application/json", json);
    });

    AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/api/control", 
    [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        String mode = jsonObj["mode"] | "stop";

        if (mode == "fan") {
            if (jsonObj.containsKey("auto")) {
                sysState.fanAutoMode = jsonObj["auto"];
            }
            if (!sysState.fanAutoMode && jsonObj.containsKey("speed")) {
                int percent = jsonObj["speed"];
                int pwmVal = map(percent, 0, 100, 0, MAX_DUTY);
                setFanSpeed(pwmVal);
            }
        }
        else if (mode == "manual") {
            sysState.effectActive = false;
            String chStr = jsonObj["channel"] | "r";
            int val = jsonObj["value"];
            if (chStr == "r") setLedRaw(0, val);
            if (chStr == "g") setLedRaw(1, val);
            if (chStr == "b") setLedRaw(2, val);
            if (chStr == "w") setLedRaw(3, val);
        }
        else if (mode == "effect") {
            sysState.effectActive = true;
            sysState.currentEffect = jsonObj["effect"].as<String>();
            sysState.effectSpeed = jsonObj["speed"] | 50;
            String t = jsonObj["led"] | "";
            sysState.effectTargetCh = (t=="r"?0 : t=="g"?1 : t=="b"?2 : t=="w"?3 : -1);
        }
        else if (mode == "stop") {
            sysState.effectActive = false;
            setAllLeds(0,0,0,0);
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.addHandler(handler);
    server.begin();
}

void setup() {
    Serial.begin(115200);
    dht.begin();

    for (int i = 0; i < 4; i++) {
        ledcSetup(i, LED_FREQ, PWM_RES);
        ledcAttachPin(ledPins[i], i);
        ledcWrite(i, 0);
    }

    ledcSetup(FAN_CHANNEL, FAN_FREQ, PWM_RES);
    ledcAttachPin(FAN_PIN, FAN_CHANNEL);
    ledcWrite(FAN_CHANNEL, 0); 

    pinMode(TACH_PIN, INPUT_PULLUP); 
    attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, FALLING);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.println("\nIP: " + WiFi.localIP().toString());

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    setupServer();
}

void loop() {
    unsigned long now = millis();

    if (!mqttClient.connected()) {
        if (now - lastMqttRetry > 15000) {
            lastMqttRetry = now;
            if (mqttClient.connect(CLIENTID, MQTT_USER, MQTT_PASSWORD)) {
                mqttClient.publish(MQTT_TOPIC_IP, WiFi.localIP().toString().c_str());
            }
        }
    } else {
        mqttClient.loop();
    }

    if (now - lastSensorRead > SENSOR_INTERVAL) {
        updateSensors();
        lastSensorRead = now;
    }

    if (now - lastFanUpdate > FAN_INTERVAL) {
        updateFanLogic();
        lastFanUpdate = now;
    }

    handleEffects();
}