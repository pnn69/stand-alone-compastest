Hardware:
 - ESP32-WROOM-32
 - ICM20948 (9-axis IMU via I²C)
 - 3× WS2813B LEDs

Pin Configuration:
 - GPIO32 → LED data
 - GPIO21 → SDA
 - GPIO22 → SCL

Connection:
 - ESP32 connected to COM51
 - This port can also be used as a debug port


WiFi Access Point:
 - SSID: NicE_WiFi
 - Password: !Ni1001100110

Requirements:
- Embedded (ESP32 firmware)
- use the espressif idf

Read magnetometer data from ICM20948
Calculate compass heading in degrees (0–360°)
Track min/max values for X, Y, Z axes
Drive LEDs:

LED 0 → green when ICM20948 is detected/initialized
LED 1 → green when heading is within ±10° of South (180°)


Host a web server (on ESP32)


Web Interface (served by ESP32)
Create a webpage with:


Compass (Wind Rose)

Visual compass dial showing heading direction
Smooth update (AJAX / WebSocket preferred)



Raw Magnetometer Data

X, Y, Z values (live)



Min / Max Values

Display min/max per axis



Bar Graph

Real-time bar graph for X, Y, Z

Use responsive design (works on mobile)
Update rate: at least 5–10 Hz
Prefer lightweight (no heavy frameworks)
Use JSON API from ESP32

