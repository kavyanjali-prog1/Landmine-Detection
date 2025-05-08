# Landmine-Detection
A smart ESP32-CAM based robot capable of live video streaming, landmine detection, GPS tracking, and remote control via a web interface.

## 📷 Overview

This project integrates a landmine detection sensor, GPS module, and ESP32-CAM for real-time surveillance and navigation. The robot streams live video, monitors landmine status, shares GPS location, and responds to directional commands over WiFi.

## 🛠 Features

- Live video streaming (MJPEG)
- GPS tracking with Google Maps link
- Landmine detection sensor integration
- LED control via web interface
- Motor control (Forward, Backward, Left, Right, Stop)
- Mobile-friendly control panel
- mDNS support (http://robot.local)

## 🧩 Hardware Used

- ESP32-CAM module
- GPS module (e.g., NEO-6M)
- Motor driver (e.g., L298N)
- Landmine sensor (digital output type)
- LED
- Power supply
- Jumper wires, chassis, wheels

## 🔌 Pin Connections

| Component         | ESP32-CAM GPIO |
|------------------|----------------|
| Landmine Sensor  | GPIO 1         |
| LED              | GPIO 2         |
| GPS RX           | GPIO 4         |
| GPS TX           | GPIO 47        |
| Camera Data Pins | GPIOs 6–18     |

## ⚙ Setup Instructions

1. Install the Arduino IDE and ESP32 board support.
2. Install required libraries:
   - TinyGPS++
   - ESP32 Camera libraries
3. Update WiFi credentials in code:
   ```cpp
   const char *ssid = "your_SSID";
   const char *password = "your_PASSWORD";
