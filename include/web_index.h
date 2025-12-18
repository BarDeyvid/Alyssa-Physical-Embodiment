#ifndef WEB_INDEX_H
#define WEB_INDEX_H

#include <pgmspace.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 LED Master</title>
    <style>
        :root { --bg: #1a1a1a; --card: #2d2d2d; --text: #e0e0e0; --primary: #007bff; }
        body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; text-align: center; }
        h1, h2 { color: #fff; margin-bottom: 10px; }
        .card { background: var(--card); padding: 20px; border-radius: 12px; margin: 15px auto; max-width: 500px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .sensor-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .sensor-val { font-size: 1.5rem; font-weight: bold; color: var(--primary); }
        
        /* Sliders */
        input[type=range] { width: 100%; margin: 10px 0; -webkit-appearance: none; background: #444; height: 8px; border-radius: 4px; }
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; background: var(--primary); border-radius: 50%; cursor: pointer; }
        
        /* Botoes */
        .btn-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap: 10px; }
        button { background: #444; color: white; border: none; padding: 12px; border-radius: 6px; cursor: pointer; transition: 0.2s; font-weight: 600; }
        button:hover { background: var(--primary); }
        button.stop { background: #d32f2f; grid-column: 1 / -1; }
        button.stop:hover { background: #b71c1c; }
        
        .status-dot { height: 10px; width: 10px; background-color: #bbb; border-radius: 50%; display: inline-block; margin-right: 5px; }
        .connected { background-color: #4CAF50; }
    </style>
</head>
<body>
    <h1>ESP32 RGBW Control</h1>
    
    <div class="card">
        <h2>Ambiente <span id="statusDot" class="status-dot"></span></h2>
        <div class="sensor-grid">
            <div>Temp: <div id="temp" class="sensor-val">--</div> &deg;C</div>
            <div>Umid: <div id="hum" class="sensor-val">--</div> %</div>
        </div>
    </div>

    <div class="card">
        <h2>Canais Individuais</h2>
        <div id="sliders">
            </div>
    </div>

    <div class="card">
        <h2>Efeitos</h2>
        <div class="btn-grid">
            <button onclick="setEffect('rainbow', {speed: 40})">Rainbow</button>
            <button onclick="setEffect('fire')">Fogo</button>
            <button onclick="setEffect('aurora')">Aurora</button>
            <button onclick="setEffect('chase', {delay: 100})">Chase</button>
            <button onclick="setEffect('police')">Polícia</button>
            <button onclick="setEffect('pulse', {led: 'r'})">Pulse Red</button>
            <button class="stop" onclick="stopEffects()">PARAR TUDO</button>
        </div>
    </div>

    <script>
        const channels = ['r', 'g', 'b', 'w'];
        let isDragging = false;

        // Gera sliders dinamicamente
        const sliderContainer = document.getElementById('sliders');
        channels.forEach(ch => {
            sliderContainer.innerHTML += `
                <label>Canal ${ch.toUpperCase()}: <span id="val-${ch}">0</span></label>
                <input type="range" id="range-${ch}" min="0" max="4095" value="0" 
                       oninput="updateLabel('${ch}', this.value)" 
                       onchange="sendPWM('${ch}', this.value)">`;
        });

        function updateLabel(ch, val) {
            document.getElementById(`val-${ch}`).innerText = val;
            isDragging = true;
        }

        // API Fetch Wrapper
        async function apiCall(endpoint, data = null) {
            try {
                const options = data ? {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(data)
                } : { method: 'GET' };
                
                const res = await fetch(endpoint, options);
                return await res.json();
            } catch (e) { console.error("Erro API:", e); return null; }
        }

        // Envia comando PWM
        function sendPWM(ch, val) {
            isDragging = false;
            apiCall('/api/control', { mode: 'manual', channel: ch, value: parseInt(val) });
        }

        // Ativa Efeito
        function setEffect(name, params = {}) {
            apiCall('/api/control', { mode: 'effect', effect: name, ...params });
        }

        function stopEffects() {
            apiCall('/api/control', { mode: 'stop' });
            channels.forEach(ch => {
                document.getElementById(`range-${ch}`).value = 0;
                document.getElementById(`val-${ch}`).innerText = 0;
            });
        }

        // Loop de Atualização de Status
        async function fetchStatus() {
            if(isDragging) return; // Não atualiza se usuário estiver movendo slider

            const data = await apiCall('/api/status');
            if (data) {
                document.getElementById('statusDot').classList.add('connected');
                document.getElementById('temp').innerText = data.temp;
                document.getElementById('hum').innerText = data.hum;
                
                // Atualiza UI se não for efeito
                if (!data.effectActive) {
                    channels.forEach((ch, i) => {
                        const el = document.getElementById(`range-${ch}`);
                        if(el.value != data.pwm[i]) {
                            el.value = data.pwm[i];
                            document.getElementById(`val-${ch}`).innerText = data.pwm[i];
                        }
                    });
                }
            } else {
                document.getElementById('statusDot').classList.remove('connected');
            }
        }

        setInterval(fetchStatus, 2000); // Atualiza a cada 2s
        window.onload = fetchStatus;
    </script>
</body>
</html>
)rawliteral";

#endif