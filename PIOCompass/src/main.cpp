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
  float pitch, roll;
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
  0.0f, 0.0f,                         // pitch, roll
  false,                              // sensor_ok
  false,                              // is_calibrating
  {0, 0, 0},                          // cal_offset
  { {1,0,0}, {0,1,0}, {0,0,1} }       // cal_matrix
};

uint8_t icm_addr = ICM20948_ADDR_1;

#include "index_html.h"

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
  server.send(200, "text/html; charset=utf-8", index_html);
}

void handleData() {
  char json[1024];
  snprintf(json, sizeof(json), 
      "{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"minX\":%.2f,\"minY\":%.2f,\"minZ\":%.2f,\"maxX\":%.2f,\"maxY\":%.2f,\"maxZ\":%.2f,\"heading\":%.2f,\"pitch\":%.2f,\"roll\":%.2f,\"ok\":%s,\"is_calibrating\":%s,"
      "\"cal_offset\":[%.2f,%.2f,%.2f],"
      "\"cal_matrix\":[[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f]]}",
      g_data.magX, g_data.magY, g_data.magZ, 
      g_data.minX, g_data.minY, g_data.minZ,
      g_data.maxX, g_data.maxY, g_data.maxZ,
      g_data.heading, 
      g_data.pitch * 180.0f / PI, 
      g_data.roll * 180.0f / PI,
      g_data.sensor_ok ? "true" : "false",
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

        // Read Accelerometer for Tilt Compensation
        Wire.beginTransmission(icm_addr);
        Wire.write(0x2D);
        Wire.endTransmission(false);
        uint8_t bytesRead = Wire.requestFrom((uint16_t)icm_addr, (uint8_t)6);
        if (bytesRead >= 6) {
          int16_t ax_raw = (Wire.read() << 8) | Wire.read();
          int16_t ay_raw = (Wire.read() << 8) | Wire.read();
          int16_t az_raw = (Wire.read() << 8) | Wire.read();
          
          static float ax_avg = 0, ay_avg = 0, az_avg = 0;
          ax_avg = ax_avg * 0.8f + ax_raw * 0.2f;
          ay_avg = ay_avg * 0.8f + ay_raw * 0.2f;
          az_avg = az_avg * 0.8f + az_raw * 0.2f;

          // Map Accel to Mag frame (AK09916 inside ICM20948)
          // Mag X = Accel Y, Mag Y = Accel X, Mag Z = -Accel Z
          float Ax = ay_avg;
          float Ay = ax_avg;
          float Az = -az_avg;

          // Normalize Accelerometer (Gravity) Vector
          float normA = sqrt(Ax*Ax + Ay*Ay + Az*Az);
          if (normA == 0) normA = 1;
          Ax /= normA;
          Ay /= normA;
          Az /= normA;

          // Calculate Pitch (theta) and Roll (phi)
          g_data.pitch = atan2f(-Ax, sqrt(Ay*Ay + Az*Az));
          g_data.roll = atan2f(Ay, Az);

          // Apply Tilt Compensation
          float Mx = g_data.magX;
          float My = g_data.magY;
          float Mz = g_data.magZ;

          float Xh = Mx * cos(g_data.pitch) + My * sin(g_data.roll)*sin(g_data.pitch) + Mz * cos(g_data.roll)*sin(g_data.pitch);
          float Yh = My * cos(g_data.roll) - Mz * sin(g_data.roll);

          // Calculate heading
          float current_h_rad = atan2f(-Yh, Xh);
          
          static float sin_samples[5] = {0};
          static float cos_samples[5] = {0};
          static int sample_idx = 0;
          static int sample_count = 0;
          
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
          // Apply hardware orientation offset if needed
          g_data.heading -= 90.0f;
          if (g_data.heading < 0) g_data.heading += 360.0f;
          if (g_data.heading >= 360.0f) g_data.heading -= 360.0f;
        }
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
