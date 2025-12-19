#ifndef WEB_INDEX_H
#define WEB_INDEX_H

#include <pgmspace.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Master Control</title>
    <style>
        :root { --bg: #1a1a1a; --card: #2d2d2d; --text: #e0e0e0; --primary: #007bff; --fan: #00c853; }
        body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; text-align: center; }
        h1, h2 { color: #fff; margin-bottom: 10px; }
        .card { background: var(--card); padding: 20px; border-radius: 12px; margin: 15px auto; max-width: 500px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .sensor-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .sensor-val { font-size: 1.5rem; font-weight: bold; color: var(--primary); }
        
        /* Sliders e Inputs */
        input[type=range] { width: 100%; margin: 10px 0; -webkit-appearance: none; background: #444; height: 8px; border-radius: 4px; }
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; background: var(--primary); border-radius: 50%; cursor: pointer; }
        input[type=range].fan-slider::-webkit-slider-thumb { background: var(--fan); }
        
        /* Toggle Switch */
        .switch { position: relative; display: inline-block; width: 50px; height: 24px; vertical-align: middle; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
        .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
        input:checked + .slider { background-color: var(--fan); }
        input:checked + .slider:before { transform: translateX(26px); }

        /* Botoes */
        .btn-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap: 10px; }
        button { background: #444; color: white; border: none; padding: 12px; border-radius: 6px; cursor: pointer; transition: 0.2s; font-weight: 600; }
        button:hover { background: var(--primary); }
        button.stop { background: #d32f2f; grid-column: 1 / -1; }
        
        .status-dot { height: 10px; width: 10px; background-color: #bbb; border-radius: 50%; display: inline-block; margin-right: 5px; }
        .connected { background-color: #4CAF50; }
        .rpm-display { font-family: monospace; font-size: 1.2rem; color: var(--fan); }
    </style>
</head>
<body>
    <h1>ESP32 System Control</h1>
    
    <div class="card">
        <h2>Ambiente <span id="statusDot" class="status-dot"></span></h2>
        <div class="sensor-grid">
            <div>Temp: <span id="temp" class="sensor-val">--</span> &deg;C</div>
            <div>Umid: <span id="hum" class="sensor-val">--</span> %</div>
        </div>
    </div>

    <div class="card">
        <h2>Refrigeração Inteligente</h2>
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;">
            <span>Modo Automático:</span>
            <label class="switch">
                <input type="checkbox" id="fanAutoToggle" onchange="toggleFanMode()">
                <span class="slider"></span>
            </label>
        </div>
        <div>
            <span class="rpm-display">RPM: <span id="fanRpm">0</span></span>
        </div>
        <label>Velocidade: <span id="val-fan">0</span>%</label>
        <input type="range" id="range-fan" class="fan-slider" min="0" max="100" value="0" 
               oninput="updateFanLabel(this.value)" onchange="sendFanPWM(this.value)">
    </div>

    <div class="card">
        <h2>Canais LED RGBW</h2>
        <div id="sliders"></div>
    </div>

    <div class="card">
        <h2>Efeitos</h2>
        <div class="btn-grid">
            <button onclick="setEffect('rainbow', {speed: 40})">Rainbow</button>
            <button onclick="setEffect('fire')">Fogo</button>
            <button onclick="setEffect('aurora')">Aurora</button>
            <button onclick="setEffect('pulse', {led: 'r'})">Pulse Red</button>
            <button class="stop" onclick="stopEffects()">PARAR LEDS</button>
        </div>
    </div>

    <script>
        const channels = ['r', 'g', 'b', 'w'];
        let isDragging = false;

        // --- Geração Dinâmica de Sliders LED ---
        const sliderContainer = document.getElementById('sliders');
        channels.forEach(ch => {
            sliderContainer.innerHTML += `
                <label>Canal ${ch.toUpperCase()}: <span id="val-${ch}">0</span></label>
                <input type="range" id="range-${ch}" min="0" max="4095" value="0" 
                       oninput="document.getElementById('val-${ch}').innerText = this.value; isDragging = true;" 
                       onchange="sendPWM('${ch}', this.value)">`;
        });

        // --- Comunicação API ---
        async function apiCall(endpoint, data = null) {
            try {
                const options = data ? {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(data)
                } : { method: 'GET' };
                return await (await fetch(endpoint, options)).json();
            } catch (e) { console.error("Erro API:", e); return null; }
        }

        function sendPWM(ch, val) {
            isDragging = false;
            apiCall('/api/control', { mode: 'manual', channel: ch, value: parseInt(val) });
        }

        function setEffect(name, params = {}) {
            apiCall('/api/control', { mode: 'effect', effect: name, ...params });
        }

        function stopEffects() {
            apiCall('/api/control', { mode: 'stop' });
        }

        // --- Lógica Fan ---
        function updateFanLabel(val) {
            document.getElementById('val-fan').innerText = val;
            isDragging = true;
        }

        function sendFanPWM(val) {
            isDragging = false;
            // Se mexer no slider, desativa o modo auto automaticamente
            document.getElementById('fanAutoToggle').checked = false;
            apiCall('/api/control', { mode: 'fan', auto: false, speed: parseInt(val) });
        }

        function toggleFanMode() {
            const isAuto = document.getElementById('fanAutoToggle').checked;
            const currentSpeed = document.getElementById('range-fan').value;
            apiCall('/api/control', { mode: 'fan', auto: isAuto, speed: parseInt(currentSpeed) });
        }

        // --- Loop de Status ---
        async function fetchStatus() {
            if(isDragging) return;

            const data = await apiCall('/api/status');
            if (data) {
                document.getElementById('statusDot').classList.add('connected');
                document.getElementById('temp').innerText = data.temp;
                document.getElementById('hum').innerText = data.hum;
                
                // Atualiza Fan
                document.getElementById('fanRpm').innerText = data.rpm;
                document.getElementById('fanAutoToggle').checked = data.fanAuto;
                
                // Só atualiza o slider da ventoinha se estiver em Auto (feedback visual)
                if(data.fanAuto) {
                    const percent = Math.round((data.fanPWM / 4095) * 100);
                    document.getElementById('range-fan').value = percent;
                    document.getElementById('val-fan').innerText = percent;
                }

                // Atualiza LEDs
                if (!data.effectActive) {
                    channels.forEach((ch, i) => {
                        document.getElementById(`range-${ch}`).value = data.pwm[i];
                        document.getElementById(`val-${ch}`).innerText = data.pwm[i];
                    });
                }
            } else {
                document.getElementById('statusDot').classList.remove('connected');
            }
        }

        setInterval(fetchStatus, 2000);
        window.onload = fetchStatus;
    </script>
</body>
</html>
)rawliteral";

#endif