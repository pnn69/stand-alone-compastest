#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 Compass (Arduino/PIO)</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: sans-serif; text-align: center; background: #f0f0f0; margin: 0; padding: 20px; }
        .container { max-width: 600px; margin: auto; padding: 20px; background: white; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        .compass { position: relative; width: 200px; height: 200px; margin: 20px auto; border: 5px solid #333; border-radius: 50%; background: white; }
        .needle { position: absolute; top: 50%; left: 50%; width: 4px; height: 100px; background: red; transform-origin: bottom; transform: translate(-50%, -100%) rotate(0deg); transition: transform 0.2s; }
        .data-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 20px; text-align: left; }
        .bar-container { height: 20px; background: #ddd; border-radius: 10px; overflow: hidden; margin: 5px 0; }
        .bar { height: 100%; transition: width 0.1s; }
        .bar-x { background: #ff4d4d; }
        .bar-y { background: #4dff4d; }
        .bar-z { background: #4d4dff; }
        h1 { font-size: 1.5rem; }
        h3 { margin-bottom: 5px; font-size: 1rem; }
        .cal-btn { padding: 10px 20px; font-size: 1rem; background: #007BFF; color: white; border: none; border-radius: 5px; cursor: pointer; margin-top: 20px; }
        .cal-btn.calibrating { background: #dc3545; animation: pulse 1s infinite; }
        @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.7; } 100% { opacity: 1; } }
    </style>
</head>
<body>
    <div class="container">
        <h1>Compass Dashboard</h1>
        <div class="compass">
            <div id="needle" class="needle"></div>
            <div style="position:absolute; top:5px; left:50%; transform:translateX(-50%); font-weight:bold;">N</div>
            <div style="position:absolute; bottom:5px; left:50%; transform:translateX(-50%); font-weight:bold;">S</div>
            <div style="position:absolute; top:50%; left:5px; transform:translateY(-50%); font-weight:bold;">W</div>
            <div style="position:absolute; top:50%; right:5px; transform:translateY(-50%); font-weight:bold;">E</div>
        </div>
        <h2 id="heading">0&deg;</h2>
        
        <div class="data-grid">
            <div>
                <h3>Live Data (Compensated)</h3>
                X: <span id="valX">0</span><br>
                Y: <span id="valY">0</span><br>
                Z: <span id="valZ">0</span><br>
                <strong>Pitch: <span id="valPitch">0</span>&deg;</strong><br>
                <strong>Roll: <span id="valRoll">0</span>&deg;</strong>
            </div>
            <div>
                <h3>Raw Min / Max</h3>
                X: <span id="minX">0</span> / <span id="maxX">0</span><br>
                Y: <span id="minY">0</span> / <span id="maxY">0</span><br>
                Z: <span id="minZ">0</span> / <span id="maxZ">0</span>
            </div>
            <div style="grid-column: 1 / -1; background: #fafafa; padding: 10px; border-radius: 5px; border: 1px solid #eee;">
                <h3 style="margin-top:0;">Calibration Parameters</h3>
                <p style="margin:5px 0;"><strong>Offsets (Hard Iron):</strong> <span id="calOffsets">[0.00, 0.00, 0.00]</span></p>
                <p style="margin:5px 0;"><strong>Matrix (Soft Iron):</strong></p>
                <div style="font-family: monospace; background:#fff; padding:5px; border:1px solid #ccc; display:inline-block;">
                    <span id="calM0">[1.000, 0.000, 0.000]</span><br>
                    <span id="calM1">[0.000, 1.000, 0.000]</span><br>
                    <span id="calM2">[0.000, 0.000, 1.000]</span>
                </div>
            </div>
        </div>

        <h3>Magnetometer Bars</h3>
        X: <div class="bar-container"><div id="barX" class="bar bar-x" style="width: 50%;"></div></div>
        Y: <div class="bar-container"><div id="barY" class="bar bar-y" style="width: 50%;"></div></div>
        Z: <div class="bar-container"><div id="barZ" class="bar bar-z" style="width: 50%;"></div></div>
        
        <button id="calBtn" class="cal-btn" onclick="toggleCal()">Start Calibration</button>
        <p id="calStatus" style="font-size: 0.9rem; color: #555; margin-top: 10px;">Rotate the device in all directions during calibration (Figure 8).</p>

        <div id="calCanvasContainer" style="display: none; margin-top: 20px;">
            <h3>Calibration Coverage</h3>
            <p style="font-size: 0.8rem; color: #666; margin-bottom: 5px;">Rotate until you form a complete circle.</p>
            <canvas id="calCanvas" width="200" height="200" style="border: 1px solid #ccc; border-radius: 5px; background: #fafafa;"></canvas>
        </div>
        
        <p id="status" style="font-size: 0.8rem; color: #666; margin-top: 20px;">Sensor: Offline</p>
    </div>

    <script>
        let currentRotation = 0;
        let isCalibrating = false;
        let canvas = document.getElementById('calCanvas');
        let ctx = canvas.getContext('2d');

        function clearCanvas() {
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.beginPath();
            ctx.strokeStyle = '#ddd';
            ctx.moveTo(100, 0); ctx.lineTo(100, 200);
            ctx.moveTo(0, 100); ctx.lineTo(200, 100);
            ctx.stroke();
            ctx.beginPath();
            ctx.arc(100, 100, 90, 0, 2 * Math.PI);
            ctx.stroke();
        }
        clearCanvas();

        function toggleCal() {
            const cmd = isCalibrating ? 'stop' : 'start';
            if (cmd === 'start') clearCanvas();
            fetch('/api/calibrate?cmd=' + cmd, { method: 'POST' })
                .then(r => r.text())
                .then(res => console.log('Calibration ' + cmd + ': ' + res))
                .catch(e => console.error(e));
        }

        function updateData() {
            fetch('/api/data')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('heading').innerText = data.heading.toFixed(1) + '°';
                    
                    let targetHeading = data.heading;
                    let diff = targetHeading - (currentRotation % 360);
                    if (diff > 180) diff -= 360;
                    if (diff < -180) diff += 360;
                    currentRotation += diff;

                    document.getElementById('needle').style.transform = `translate(-50%, -100%) rotate(${currentRotation}deg)`;
                    
                    document.getElementById('valX').innerText = data.x.toFixed(2);
                    document.getElementById('valY').innerText = data.y.toFixed(2);
                    document.getElementById('valZ').innerText = data.z.toFixed(2);
                    if (data.pitch !== undefined) {
                        document.getElementById('valPitch').innerText = data.pitch.toFixed(1);
                        document.getElementById('valRoll').innerText = data.roll.toFixed(1);
                    }

                    document.getElementById('minX').innerText = data.minX.toFixed(2);
                    document.getElementById('minY').innerText = data.minY.toFixed(2);
                    document.getElementById('minZ').innerText = data.minZ.toFixed(2);
                    document.getElementById('maxX').innerText = data.maxX.toFixed(2);
                    document.getElementById('maxY').innerText = data.maxY.toFixed(2);
                    document.getElementById('maxZ').innerText = data.maxZ.toFixed(2);
                    
                    if (data.cal_offset) {
                        document.getElementById('calOffsets').innerText = `[${data.cal_offset[0].toFixed(2)}, ${data.cal_offset[1].toFixed(2)}, ${data.cal_offset[2].toFixed(2)}]`;
                        document.getElementById('calM0').innerText = `[${data.cal_matrix[0][0].toFixed(3)}, ${data.cal_matrix[0][1].toFixed(3)}, ${data.cal_matrix[0][2].toFixed(3)}]`;
                        document.getElementById('calM1').innerText = `[${data.cal_matrix[1][0].toFixed(3)}, ${data.cal_matrix[1][1].toFixed(3)}, ${data.cal_matrix[1][2].toFixed(3)}]`;
                        document.getElementById('calM2').innerText = `[${data.cal_matrix[2][0].toFixed(3)}, ${data.cal_matrix[2][1].toFixed(3)}, ${data.cal_matrix[2][2].toFixed(3)}]`;
                    }

                    const mapBar = (v) => Math.min(100, Math.max(0, (v + 60) / 1.2));
                    document.getElementById('barX').style.width = mapBar(data.x) + '%';
                    document.getElementById('barY').style.width = mapBar(data.y) + '%';
                    document.getElementById('barZ').style.width = mapBar(data.z) + '%';
                    
                    document.getElementById('status').innerText = "Sensor: " + (data.ok ? "Online" : "Error");
                    document.getElementById('status').style.color = data.ok ? "green" : "red";

                    isCalibrating = data.is_calibrating;
                    const btn = document.getElementById('calBtn');
                    const canvasContainer = document.getElementById('calCanvasContainer');
                    
                    if (isCalibrating) {
                        btn.innerText = "Stop Calibration & Save";
                        btn.classList.add('calibrating');
                        canvasContainer.style.display = 'block';
                        
                        // Plot X/Y raw data
                        let px = 100 + (data.x / 60) * 90;
                        let py = 100 - (data.y / 60) * 90;
                        ctx.fillStyle = '#ff4d4d';
                        ctx.fillRect(px, py, 3, 3);
                    } else {
                        btn.innerText = "Start Calibration";
                        btn.classList.remove('calibrating');
                        canvasContainer.style.display = 'none';
                    }
                })
                .catch(e => {
                    document.getElementById('status').innerText = "Status: Connection Error";
                    document.getElementById('status').style.color = "red";
                });
        }
        setInterval(updateData, 200);
    </script>
</body>
</html>
)rawliteral";

#endif // INDEX_HTML_H