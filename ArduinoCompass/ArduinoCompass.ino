#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

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

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

// Data Structure
struct CompassData {
  float magX, magY, magZ;
  float minX, minY, minZ;
  float maxX, maxY, maxZ;
  float heading;
  bool sensor_ok;
};

CompassData g_data = {
  0.0f, 0.0f, 0.0f,                   // magX, magY, magZ
  10000.0f, 10000.0f, 10000.0f,       // minX, minY, minZ
  -10000.0f, -10000.0f, -10000.0f,    // maxX, maxY, maxZ
  0.0f,                               // heading
  false                               // sensor_ok
};

uint8_t icm_addr = ICM20948_ADDR_1;

// HTML Content (Raw String Literal)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Compass (Arduino)</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: sans-serif; text-align: center; background: #f0f0f0; margin: 0; padding: 20px; }
        .container { max-width: 600px; margin: auto; padding: 20px; background: white; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        .compass { position: relative; width: 200px; height: 200px; margin: 20px auto; border: 5px solid #333; border-radius: 50%; background: white; }
        .needle { position: absolute; top: 50%; left: 50%; width: 4px; height: 100px; background: red; transform-origin: bottom; transform: translate(-50%, -100%) rotate(0deg); }
        .data-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 20px; text-align: left; }
        .bar-container { height: 20px; background: #ddd; border-radius: 10px; overflow: hidden; margin: 5px 0; }
        .bar { height: 100%; transition: width 0.1s; }
        .bar-x { background: #ff4d4d; }
        .bar-y { background: #4dff4d; }
        .bar-z { background: #4d4dff; }
        h1 { font-size: 1.5rem; }
        h3 { margin-bottom: 5px; font-size: 1rem; }
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
        <h2 id="heading">0°</h2>
        
        <div class="data-grid">
            <div>
                <h3>Live Data (uT)</h3>
                X: <span id="valX">0</span><br>
                Y: <span id="valY">0</span><br>
                Z: <span id="valZ">0</span>
            </div>
            <div>
                <h3>Min / Max</h3>
                X: <span id="minX">0</span> / <span id="maxX">0</span><br>
                Y: <span id="minY">0</span> / <span id="maxY">0</span><br>
                Z: <span id="minZ">0</span> / <span id="maxZ">0</span>
            </div>
        </div>

        <h3>Magnetometer Bars</h3>
        X: <div class="bar-container"><div id="barX" class="bar bar-x" style="width: 50%;"></div></div>
        Y: <div class="bar-container"><div id="barY" class="bar bar-y" style="width: 50%;"></div></div>
        Z: <div class="bar-container"><div id="barZ" class="bar bar-z" style="width: 50%;"></div></div>
        <p id="status" style="font-size: 0.8rem; color: #666; margin-top: 20px;">Sensor: Offline</p>
    </div>

    <script>
        let currentRotation = 0;

        function updateData() {
            fetch('/api/data')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('heading').innerText = data.heading.toFixed(1) + '°';
                    
                    // Prevent 360-degree spin when crossing North
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

                    const mapBar = (v) => Math.min(100, Math.max(0, (v + 60) / 1.2));
                    document.getElementById('barX').style.width = mapBar(data.x) + '%';
                    document.getElementById('barY').style.width = mapBar(data.y) + '%';
                    document.getElementById('barZ').style.width = mapBar(data.z) + '%';
                    
                    document.getElementById('status').innerText = "Sensor: " + (data.ok ? "Online" : "Error");
                    document.getElementById('status').style.color = data.ok ? "green" : "red";
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
  server.send(200, "text/html", index_html);
}

void handleData() {
  char json[256];
  snprintf(json, sizeof(json), 
      "{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"minX\":%.2f,\"minY\":%.2f,\"minZ\":%.2f,\"maxX\":%.2f,\"maxY\":%.2f,\"maxZ\":%.2f,\"heading\":%.2f,\"ok\":%s}",
      g_data.magX, g_data.magY, g_data.magZ, 
      g_data.minX, g_data.minY, g_data.minZ,
      g_data.maxX, g_data.maxY, g_data.maxZ,
      g_data.heading, g_data.sensor_ok ? "true" : "false");
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

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

  server.on("/", handleRoot);
  server.on("/api/data", handleData);
  server.begin();
}

void loop() {
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

        g_data.magX = x * 0.15f;
        g_data.magY = y * 0.15f;
        g_data.magZ = z * 0.15f;

        if (g_data.magX < g_data.minX) g_data.minX = g_data.magX;
        if (g_data.magY < g_data.minY) g_data.minY = g_data.magY;
        if (g_data.magZ < g_data.minZ) g_data.minZ = g_data.magZ;
        if (g_data.magX > g_data.maxX) g_data.maxX = g_data.magX;
        if (g_data.magY > g_data.maxY) g_data.maxY = g_data.magY;
        if (g_data.magZ > g_data.maxZ) g_data.maxZ = g_data.magZ;

        // Calculate heading using Y and Z axes with circular averaging (10 samples)
        static float sin_samples[10] = {0};
        static float cos_samples[10] = {0};
        static int sample_idx = 0;
        static int sample_count = 0;

        float current_h_rad = atan2f(g_data.magZ, g_data.magY);
        sin_samples[sample_idx] = sinf(current_h_rad);
        cos_samples[sample_idx] = cosf(current_h_rad);
        
        sample_idx = (sample_idx + 1) % 10;
        if (sample_count < 10) sample_count++;

        float sum_sin = 0, sum_cos = 0;
        for (int i = 0; i < sample_count; i++) {
          sum_sin += sin_samples[i];
          sum_cos += cos_samples[i];
        }

        g_data.heading = atan2f(sum_sin, sum_cos) * 180.0f / PI;
        if (g_data.heading < 0) g_data.heading += 360.0f;
      }
    }

    // Update LEDs
    if (g_data.sensor_ok) {
      strip.setPixelColor(0, strip.Color(0, 255, 0)); // Green
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
