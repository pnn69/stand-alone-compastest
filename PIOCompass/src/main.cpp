#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

// Configuration
const char* ssid = "NicE_WiFi";
const char* password = "!Ni1001100110";

#define I2C_SDA 21
#define I2C_SCL 22

#define ICM20948_ADDR_1 0x68
#define ICM20948_ADDR_2 0x69
#define AK09916_ADDR 0x0C

#define LED_PIN 32
#define LED_COUNT 3
#define AVERIDGE_COUNT 3

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences preferences;

// Data Structure
struct CompassData {
  float magX, magY, magZ;
  float minX, minY, minZ;
  float maxX, maxY, maxZ;
  float heading;
  bool sensor_ok;
  
  bool is_calibrating;
  float cal_offset[3];
  float cal_matrix[3][3];
};

CompassData g_data = {
  0.0f, 0.0f, 0.0f,                   // magX, magY, magZ
  10000.0f, 10000.0f, 10000.0f,       // minX, minY, minZ
  -10000.0f, -10000.0f, -10000.0f,    // maxX, maxY, maxZ
  0.0f,                               // heading
  false,                              // sensor_ok
  false,                              // is_calibrating
  {0, 0, 0},                          // cal_offset
  { {1,0,0}, {0,1,0}, {0,0,1} }       // cal_matrix
};

uint8_t icm_addr = ICM20948_ADDR_1;

// HTML Content (Raw String Literal)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
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
                Z: <span id="valZ">0</span>
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
        
        <p id="status" style="font-size: 0.8rem; color: #666; margin-top: 20px;">Sensor: Offline</p>
    </div>

    <script>
        let currentRotation = 0;
        let isCalibrating = false;

        function toggleCal() {
            const cmd = isCalibrating ? 'stop' : 'start';
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
                    if (isCalibrating) {
                        btn.innerText = "Stop Calibration & Save";
                        btn.classList.add('calibrating');
                    } else {
                        btn.innerText = "Start Calibration";
                        btn.classList.remove('calibrating');
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

void writeI2C(uint8_t dev_addr, uint8_t reg, uint8_t data) {
  Wire.beginTransmission(dev_addr);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void loadCalibration() {
  preferences.begin("compass", false); // read-only false
  if (preferences.getBytesLength("cal_offset") == sizeof(g_data.cal_offset)) {
    preferences.getBytes("cal_offset", g_data.cal_offset, sizeof(g_data.cal_offset));
  }
  if (preferences.getBytesLength("cal_matrix") == sizeof(g_data.cal_matrix)) {
    preferences.getBytes("cal_matrix", g_data.cal_matrix, sizeof(g_data.cal_matrix));
  }
  preferences.end();
}

void saveCalibration() {
  preferences.begin("compass", false);
  preferences.putBytes("cal_offset", g_data.cal_offset, sizeof(g_data.cal_offset));
  preferences.putBytes("cal_matrix", g_data.cal_matrix, sizeof(g_data.cal_matrix));
  preferences.end();
  Serial.println("Calibration saved to Preferences.");
}

void initSensor() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  
  // Probe ICM20948
  Wire.beginTransmission(ICM20948_ADDR_1);
  if (Wire.endTransmission() == 0) {
    icm_addr = ICM20948_ADDR_1;
    Serial.println("Found ICM at 0x68");
  } else {
    Wire.beginTransmission(ICM20948_ADDR_2);
    if (Wire.endTransmission() == 0) {
      icm_addr = ICM20948_ADDR_2;
      Serial.println("Found ICM at 0x69");
    } else {
      Serial.println("ICM20948 not found!");
      return;
    }
  }

  // Init sequence
  writeI2C(icm_addr, 0x7F, 0x00); // Bank 0
  writeI2C(icm_addr, 0x06, 0x81); // Reset
  delay(100);
  writeI2C(icm_addr, 0x06, 0x01); // Wake
  writeI2C(icm_addr, 0x0F, 0x02); // Bypass

  // Check AK09916
  Wire.beginTransmission(AK09916_ADDR);
  Wire.write(0x01); // WIA2
  Wire.endTransmission(false);
  Wire.requestFrom(AK09916_ADDR, 1);
  if (Wire.available()) {
    uint8_t whoami = Wire.read();
    Serial.printf("AK09916 WIA2: 0x%02X\n", whoami);
    g_data.sensor_ok = (whoami == 0x09 || whoami == 0x48);
  }

  // Continuous mode 4 (100Hz)
  writeI2C(AK09916_ADDR, 0x31, 0x08);
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/html", index_html);
}

void handleData() {
  char json[1024];
  snprintf(json, sizeof(json), 
      "{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"minX\":%.2f,\"minY\":%.2f,\"minZ\":%.2f,\"maxX\":%.2f,\"maxY\":%.2f,\"maxZ\":%.2f,\"heading\":%.2f,\"ok\":%s,\"is_calibrating\":%s,"
      "\"cal_offset\":[%.2f,%.2f,%.2f],"
      "\"cal_matrix\":[[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f]]}",
      g_data.magX, g_data.magY, g_data.magZ, 
      g_data.minX, g_data.minY, g_data.minZ,
      g_data.maxX, g_data.maxY, g_data.maxZ,
      g_data.heading, g_data.sensor_ok ? "true" : "false",
      g_data.is_calibrating ? "true" : "false",
      g_data.cal_offset[0], g_data.cal_offset[1], g_data.cal_offset[2],
      g_data.cal_matrix[0][0], g_data.cal_matrix[0][1], g_data.cal_matrix[0][2],
      g_data.cal_matrix[1][0], g_data.cal_matrix[1][1], g_data.cal_matrix[1][2],
      g_data.cal_matrix[2][0], g_data.cal_matrix[2][1], g_data.cal_matrix[2][2]);
  server.send(200, "application/json", json);
}

void handleCalibrate() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    if (cmd == "start") {
      g_data.is_calibrating = true;
      g_data.minX = 10000; g_data.minY = 10000; g_data.minZ = 10000;
      g_data.maxX = -10000; g_data.maxY = -10000; g_data.maxZ = -10000;
      server.send(200, "text/plain", "Started");
      return;
    } else if (cmd == "stop") {
      g_data.is_calibrating = false;
      float offset_x = (g_data.maxX + g_data.minX) / 2.0f;
      float offset_y = (g_data.maxY + g_data.minY) / 2.0f;
      float offset_z = (g_data.maxZ + g_data.minZ) / 2.0f;
      
      float delta_x = (g_data.maxX - g_data.minX) / 2.0f;
      float delta_y = (g_data.maxY - g_data.minY) / 2.0f;
      float delta_z = (g_data.maxZ - g_data.minZ) / 2.0f;
      
      if(delta_x == 0) delta_x = 1;
      if(delta_y == 0) delta_y = 1;
      if(delta_z == 0) delta_z = 1;

      float avg_delta = (delta_x + delta_y + delta_z) / 3.0f;

      g_data.cal_offset[0] = offset_x;
      g_data.cal_offset[1] = offset_y;
      g_data.cal_offset[2] = offset_z;

      memset(g_data.cal_matrix, 0, sizeof(g_data.cal_matrix));
      g_data.cal_matrix[0][0] = avg_delta / delta_x;
      g_data.cal_matrix[1][1] = avg_delta / delta_y;
      g_data.cal_matrix[2][2] = avg_delta / delta_z;

      saveCalibration();
      server.send(200, "text/plain", "Stopped and saved");
      return;
    }
  }
  server.send(400, "text/plain", "Invalid command");
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  loadCalibration();
  initSensor();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname("Top");
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/api/data", handleData);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  static unsigned long last_read = 0;
  if (millis() - last_read >= 100) { // 10Hz
    last_read = millis();

    if (g_data.sensor_ok) {
      Wire.beginTransmission(AK09916_ADDR);
      Wire.write(0x11); // HXL
      Wire.endTransmission(false);
      Wire.requestFrom(AK09916_ADDR, 8); // Read 8 bytes
      
      if (Wire.available() >= 8) {
        uint8_t data[8];
        for (int i=0; i<8; i++) data[i] = Wire.read();

        int16_t x = (data[1] << 8) | data[0];
        int16_t y = (data[3] << 8) | data[2];
        int16_t z = (data[5] << 8) | data[4];

        float rawX = x * 0.15f;
        float rawY = y * 0.15f;
        float rawZ = z * 0.15f;

        if (g_data.is_calibrating) {
          if (rawX < g_data.minX) g_data.minX = rawX;
          if (rawY < g_data.minY) g_data.minY = rawY;
          if (rawZ < g_data.minZ) g_data.minZ = rawZ;
          if (rawX > g_data.maxX) g_data.maxX = rawX;
          if (rawY > g_data.maxY) g_data.maxY = rawY;
          if (rawZ > g_data.maxZ) g_data.maxZ = rawZ;
        }

        // Apply Hard Iron Offset
        float hx = rawX - g_data.cal_offset[0];
        float hy = rawY - g_data.cal_offset[1];
        float hz = rawZ - g_data.cal_offset[2];

        // Apply Soft Iron Matrix
        float compX = hx * g_data.cal_matrix[0][0] + hy * g_data.cal_matrix[0][1] + hz * g_data.cal_matrix[0][2];
        float compY = hx * g_data.cal_matrix[1][0] + hy * g_data.cal_matrix[1][1] + hz * g_data.cal_matrix[1][2];
        float compZ = hx * g_data.cal_matrix[2][0] + hy * g_data.cal_matrix[2][1] + hz * g_data.cal_matrix[2][2];

        // Moving Average for compensated X, Y, Z (5 samples)
        static float avgX_samples[5] = {0}, avgY_samples[5] = {0}, avgZ_samples[5] = {0};
        static int avg_idx = 0;
        static int avg_count = 0;

        avgX_samples[avg_idx] = compX;
        avgY_samples[avg_idx] = compY;
        avgZ_samples[avg_idx] = compZ;
        avg_idx = (avg_idx + 1) % AVERIDGE_COUNT;
        if (avg_count < AVERIDGE_COUNT) avg_count++;

        float sumX = 0, sumY = 0, sumZ = 0;
        for (int i = 0; i < avg_count; i++) {
          sumX += avgX_samples[i];
          sumY += avgY_samples[i];
          sumZ += avgZ_samples[i];
        }

        g_data.magX = sumX / avg_count;
        g_data.magY = sumY / avg_count;
        g_data.magZ = sumZ / avg_count;

        // Calculate heading using Y and Z axes with circular averaging (5 samples)
        static float sin_samples[5] = {0};
        static float cos_samples[5] = {0};
        static int sample_idx = 0;
        static int sample_count = 0;

        float current_h_rad = atan2f(g_data.magZ, g_data.magY);
        sin_samples[sample_idx] = sinf(current_h_rad);
        cos_samples[sample_idx] = cosf(current_h_rad);
        
        sample_idx = (sample_idx + 1) % AVERIDGE_COUNT;
        if (sample_count < AVERIDGE_COUNT) sample_count++;

        float sum_sin = 0, sum_cos = 0;
        for (int i = 0; i < sample_count; i++) {
          sum_sin += sin_samples[i];
          sum_cos += cos_samples[i];
        }

        g_data.heading = atan2f(sum_sin, sum_cos) * 180.0f / PI;
        g_data.heading -= 90.0f; // -90-degree offset
        if (g_data.heading < 0) g_data.heading += 360.0f;
        if (g_data.heading >= 360.0f) g_data.heading -= 360.0f;
      }
    }

    // Update LEDs
    if (g_data.sensor_ok) {
      if (g_data.is_calibrating) {
        strip.setPixelColor(0, strip.Color(255, 255, 0)); // Yellow
      } else {
        strip.setPixelColor(0, strip.Color(0, 255, 0)); // Green
      }
    } else {
      strip.setPixelColor(0, strip.Color(255, 0, 0)); // Red
    }

    if (abs(g_data.heading - 180.0f) <= 10.0f) {
      strip.setPixelColor(1, strip.Color(0, 255, 0)); // South
    } else {
      strip.setPixelColor(1, strip.Color(0, 0, 0));
    }
    strip.show();
  }
}
