/**
 * A foto desse projeto montado esta dentro de images
 * Irei Futuramente adicionar O passo a passo de como
 * Montar fisicamente e garantir que o esp tenha 
 * energia o bastante pra todos os 27 LEDs
 */
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>   // Para sin() e outras funções matemáticas
#include <stdlib.h> // Para random()
#include "secrets.h"

#define LEDC_TIMER_12_BIT 12
#define LEDC_BASE_FREQ 5000

// Defina as credenciais da sua rede Wi-Fi
const char* ssid = WIFI_SSID;       // Puxa do secrets.h
const char* password = WIFI_PASSWORD; // Puxa do secrets.h

// Defina os GPIOs que você soldou no ESP32
#define LED_R 33
#define LED_G 32
#define LED_B 25
#define LED_W 26  // Opcional

// Mapeamento dos nomes dos LEDs para os pinos GPIO
const int ledPins[] = {LED_R, LED_G, LED_B, LED_W};
const char* ledNames[] = {"r", "g", "b", "w"};
const int numLeds = sizeof(ledPins) / sizeof(ledPins[0]);

// Objeto do servidor web na porta 80
WebServer server(80);

// --- Variáveis Globais de Controle de Efeitos ---
volatile bool effectActive = false; 
volatile unsigned long effectStartTime = 0;
volatile float effectDuration = 0; // Duração do efeito em segundos (0 para infinito)
volatile int effectTargetLedPin = -1; // LED alvo para efeitos individuais (Breathing, Strobe, Flicker)

String currentEffectName = ""; // Nome do efeito ativo ("breathing", "strobe", "rainbow", "fire", "chase")

// Variáveis para Efeito Strobe (declaradas globalmente)
volatile unsigned long lastStrobeToggleTime = 0;
volatile int strobePeriod = 100; // ms (intervalo entre ligar/desligar)
volatile bool strobeLedState = false; // true = on, false = off
volatile int strobeTargetLedPin = -1; // LED alvo para o strobe

// Variáveis para Efeito Rainbow (apenas para RGB, W será ignorado)
volatile float rainbowHue = 0.0; // 0-360 para o ciclo de cores
volatile int rainbowSpeed = 50; // Velocidade do ciclo em ms

// Variáveis para Efeito Fire Flicker
volatile unsigned long lastFlickerTime = 0;
volatile int flickerMin = 500; // Intensidade mínima para a chama
volatile int flickerMax = 4095; // Intensidade máxima para a chama
volatile int flickerDelayMin = 50; // Atraso mínimo entre as mudanças (ms)
volatile int flickerDelayMax = 150; // Atraso máximo entre as mudanças (ms)

// Variáveis para Efeito Chase
volatile unsigned long lastChaseUpdateTime = 0;
volatile int chaseDelay = 150; // Atraso entre os LEDs (ms)
volatile int currentChaseLed = 0; // Índice do LED ativo na sequência


// --- Funções Auxiliares ---
void setAllLeds(int r_val, int g_val, int b_val, int w_val) {
  ledcWrite(LED_R, r_val);
  ledcWrite(LED_G, g_val);
  ledcWrite(LED_B, b_val);
  // Verifique se o LED_W é diferente de 0, caso ele não esteja definido.
  // Embora você tenha definido, é uma boa prática para robustez.
  if (LED_W != 0) { 
    ledcWrite(LED_W, w_val);
  }
}

// Converte HSV para RGB (Hue, Saturation, Value -> Red, Green, Blue)
// h: 0-360, s: 0-1, v: 0-1
// Retorna um array de 3 ints para RGB
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
  
  // Garante que os valores estejam dentro do range 0-4095
  for(int i=0; i<3; i++) {
    if(rgb[i] < 0) rgb[i] = 0;
    if(rgb[i] > 4095) rgb[i] = 4095;
  }

  return rgb;
}

// --- Funções de Efeito ---

void runBreathingEffect() {
  // Compara String não-volatile diretamente
  if (currentEffectName != "breathing") return; 

  unsigned long currentTime = millis();
  float elapsedSeconds = (currentTime - effectStartTime) / 1000.0;

  if (effectDuration > 0 && elapsedSeconds >= effectDuration) {
    effectActive = false;
    ledcWrite(effectTargetLedPin, 0);
    Serial.println("Efeito breathing finalizado.");
    currentEffectName = ""; // Atribuição direta
    return;
  }
  
  float breathingPeriod = 2.0; // 2 segundos para um ciclo completo
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
    // Agora strobeTargetLedPin é global
    ledcWrite(strobeTargetLedPin, strobeLedState ? 4095 : 0); 
    lastStrobeToggleTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    ledcWrite(strobeTargetLedPin, 0);
    Serial.println("Efeito strobe finalizado.");
    currentEffectName = "";
  }
}

void runRainbowCycleEffect() {
  if (currentEffectName != "rainbow") return;

  unsigned long currentTime = millis();
  if (currentTime - lastChaseUpdateTime >= rainbowSpeed) {
    rainbowHue += 1.0; // Aumenta o matiz (hue)
    if (rainbowHue >= 360.0) {
      rainbowHue = 0.0; // Reseta o matiz quando completa o ciclo
    }

    int* rgb = hsvToRgb(rainbowHue, 1.0, 1.0); // Saturação e valor máximos
    ledcWrite(LED_R, rgb[0]);
    ledcWrite(LED_G, rgb[1]);
    ledcWrite(LED_B, rgb[2]);
    // LED W é ignorado no efeito Rainbow
    if (LED_W != 0) { // Se o LED_W estiver definido, certifique-se de que ele esteja desligado
      ledcWrite(LED_W, 0); 
    }

    lastChaseUpdateTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    setAllLeds(0,0,0,0); // Desliga os LEDs no final
    Serial.println("Efeito rainbow finalizado.");
    currentEffectName = "";
  }
}

void runFireFlickerEffect() {
  if (currentEffectName != "fire") return;

  unsigned long currentTime = millis();
  if (currentTime - lastFlickerTime >= random(flickerDelayMin, flickerDelayMax)) {
    int intensityR = random(flickerMin, flickerMax);
    int intensityG = random(flickerMin / 4, flickerMax / 2); // Verde menos intenso para fogo
    int intensityB = 0; // Azul geralmente ausente no fogo
    int intensityW = random(flickerMin / 2, flickerMax); // Branco para simular brilho/calor

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
    setAllLeds(0,0,0,0); // Desliga os LEDs no final
    Serial.println("Efeito fire finalizado.");
    currentEffectName = "";
  }
}

void runChaseEffect() {
  if (currentEffectName != "chase") return;

  unsigned long currentTime = millis();
  if (currentTime - lastChaseUpdateTime >= chaseDelay) {
    setAllLeds(0,0,0,0); // Desliga todos os LEDs

    // Acende o LED atual na sequência
    ledcWrite(ledPins[currentChaseLed], 4095); // Acende com força total

    currentChaseLed = (currentChaseLed + 1) % numLeds; // Avança para o próximo LED

    lastChaseUpdateTime = currentTime;
  }

  if (effectDuration > 0 && (currentTime - effectStartTime) / 1000.0 >= effectDuration) {
    effectActive = false;
    setAllLeds(0,0,0,0); // Desliga os LEDs no final
    Serial.println("Efeito chase finalizado.");
    currentEffectName = "";
  }
}


// --- Funções de Manipulação HTTP ---

void handleControl() {
  // Parâmetro de efeito
  if (server.hasArg("effect")) {
    String effectName = server.arg("effect");
    Serial.print("Efeito solicitado: ");
    Serial.println(effectName);

    // Antes de iniciar um novo efeito, desativa qualquer efeito anterior
    effectActive = false;
    currentEffectName = ""; // Limpa o nome do efeito anterior
    setAllLeds(0,0,0,0); // Desliga todos os LEDs para garantir um estado limpo

    effectDuration = server.hasArg("duration") ? server.arg("duration").toFloat() : 0; // Duração em segundos, 0 para infinito

    if (effectName.equalsIgnoreCase("breathing")) {
      String targetLed = server.hasArg("led") ? server.arg("led") : "r";
      effectTargetLedPin = -1; // Reset para o novo efeito
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
        String responseMsg = "Efeito breathing iniciado no LED ";
        String targetLedUpper = targetLed;
        targetLedUpper.toUpperCase();
        responseMsg += targetLedUpper;
        server.send(200, "text/plain", responseMsg);
        Serial.println("Efeito breathing iniciado.");
        return;
      } else {
        server.send(400, "text/plain", "LED alvo para breathing nao encontrado.");
        return;
      }
    } else if (effectName.equalsIgnoreCase("strobe")) {
      String targetLed = server.hasArg("led") ? server.arg("led") : "r";
      effectTargetLedPin = -1; // Reutilizamos effectTargetLedPin
      for (int i = 0; i < numLeds; i++) {
        if (targetLed.equalsIgnoreCase(ledNames[i])) {
          effectTargetLedPin = ledPins[i];
          break;
        }
      }

      if (effectTargetLedPin != -1) {
        effectActive = true;
        currentEffectName = "strobe";
        strobeTargetLedPin = effectTargetLedPin; // Atribui ao strobeTargetLedPin global
        strobePeriod = server.hasArg("period") ? server.arg("period").toInt() : 100; // Parametro para o periodo
        lastStrobeToggleTime = millis();
        strobeLedState = true;
        
        String responseMsg = "Efeito strobe iniciado no LED ";
        String targetLedUpper = targetLed;
        targetLedUpper.toUpperCase();
        responseMsg += targetLedUpper;
        server.send(200, "text/plain", responseMsg);
        Serial.println("Efeito strobe iniciado.");
        return;
      } else {
        server.send(400, "text/plain", "LED alvo para strobe nao encontrado.");
        return;
      }
    } else if (effectName.equalsIgnoreCase("rainbow")) {
        effectActive = true;
        currentEffectName = "rainbow";
        effectStartTime = millis();
        rainbowHue = 0.0; // Reinicia o ciclo de cores
        rainbowSpeed = server.hasArg("speed") ? server.arg("speed").toInt() : 50; // Velocidade do ciclo
        server.send(200, "text/plain", "Efeito rainbow iniciado.");
        Serial.println("Efeito rainbow iniciado.");
        return;
    } else if (effectName.equalsIgnoreCase("fire")) {
        effectActive = true;
        currentEffectName = "fire";
        effectStartTime = millis();
        randomSeed(analogRead(0)); // Garante aleatoriedade
        lastFlickerTime = millis();
        server.send(200, "text/plain", "Efeito fire iniciado.");
        Serial.println("Efeito fire iniciado.");
        return;
    } else if (effectName.equalsIgnoreCase("chase")) {
        effectActive = true;
        currentEffectName = "chase";
        effectStartTime = millis();
        chaseDelay = server.hasArg("delay") ? server.arg("delay").toInt() : 150; // Velocidade da perseguição
        currentChaseLed = 0; // Começa pelo primeiro LED
        setAllLeds(0,0,0,0); // Limpa LEDs antes de iniciar o chase
        server.send(200, "text/plain", "Efeito chase iniciado.");
        Serial.println("Efeito chase iniciado.");
        return;
    }
    else {
      server.send(400, "text/plain", "Efeito desconhecido.");
      return;
    }
  }

  // Se não houver parâmetro de efeito, processa os parâmetros de intensidade individual
  // E desativa qualquer efeito que estivesse ativo
  effectActive = false; 
  currentEffectName = ""; // Limpa o nome do efeito ativo
  
  bool ledControlled = false;
  for (int i = 0; i < numLeds; i++) {
    String paramName = String(ledNames[i]) + "_intensity";
    if (server.hasArg(paramName)) {
      int intensity = server.arg(paramName).toInt();
      if (intensity < 0) intensity = 0;
      if (intensity > 4095) intensity = 4095;
      ledcWrite(ledPins[i], intensity);
      Serial.print("LED ");
      Serial.print(ledNames[i]);
      Serial.print(" ajustado para intensidade: ");
      Serial.println(intensity);
      ledControlled = true;
    }
  }

  if (ledControlled) {
    server.send(200, "text/plain", "LEDs ajustados.");
  } else {
    server.send(400, "text/plain", "Nenhum parametro de LED ou efeito valido fornecido.");
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
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<h1>Controle de LEDs RGBW</h1>";

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
  html += "<button class='effect-btn' onclick='startEffect(\"strobe\", \"r\", 0, 50)'>Vermelho (Rápido)</button>"; // duration=0 (infinite), period=50
  html += "<button class='effect-btn' onclick='startEffect(\"strobe\", \"g\", 0, 200)'>Verde (Lento)</button>"; // duration=0 (infinite), period=200
  html += "</div>";
  
  html += "<div>";
  html += "<h3>Rainbow Cycle</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"rainbow\", \"\", 0, 30)'>Iniciar Rainbow</button>"; // duration=0, speed=30ms
  html += "</div>";

  html += "<div>";
  html += "<h3>Fire Flicker</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"fire\")'>Iniciar Fogo</button>"; // duration=0
  html += "</div>";

  html += "<div>";
  html += "<h3>Chase</h3>";
  html += "<button class='effect-btn' onclick='startEffect(\"chase\", \"\", 0, 100)'>Iniciar Perseguição</button>"; // duration=0, delay=100ms
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

  // Função genérica para iniciar efeitos
  html += "function startEffect(effectName, ledTarget = '', duration = 0, extraParam = 0) {"; // extraParam para speed/period/delay
  html += "  var xhr = new XMLHttpRequest();";
  html += "  var url = '/control?effect=' + effectName;";
  html += "  if (ledTarget) url += '&led=' + ledTarget;";
  html += "  if (duration > 0) url += '&duration=' + duration;";
  
  // Parâmetro extra para velocidade (rainbow) ou período (strobe) ou delay (chase)
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
  html += "  alert('Efeito ' + effectName + ' iniciado.');";
  html += "}";

  html += "function stopEffects() {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/control?r_intensity=0&g_intensity=0&b_intensity=0&w_intensity=0', true);"; 
  html += "  xhr.send();";
  html += "  alert('Efeitos parados e LEDs desligados.');";
  html += "}";

  html += "</script>";
  html += "</body>";
  html += "</html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  // Inicia os canais PWM para os LEDs
  for (int i = 0; i < numLeds; i++) {
    ledcAttachPin(ledPins[i], LEDC_BASE_FREQ);
    ledcWrite(ledPins[i], 0); // Desliga todos os LEDs no início
  }

  Serial.print("Conectando-se ao Wi-Fi ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());

  // Configura as rotas do servidor
  server.on("/", handleRoot);
  server.on("/control", handleControl); // Novo endpoint para controle

  // Inicia o servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient(); // Lida com as requisições HTTP

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
}