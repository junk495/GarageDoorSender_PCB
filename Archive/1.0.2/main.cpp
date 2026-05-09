#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>

// WiFi & ESP-NOW
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h" 

// Sensor-Bibliotheken
#include <Adafruit_BME280.h>
#include <Adafruit_ADXL345_U.h>
#include <Adafruit_Fingerprint.h>

// ============== ESP-NOW KONFIGURATION ==============
#define ESPNOW_CHANNEL 6
uint8_t receiverMac[] = {0xB4, 0xE6, 0x2D, 0x97, 0x76, 0x21};

struct SensorMessage {
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
};

struct FingerEvent {
  uint8_t type = 99;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID = 1;
  uint8_t torStatus;
};

// ============= SENSOR & PROJEKT KONFIGURATION =============
#define I2C_SDA 8
#define I2C_SCL 5
#define FP_RX 7
#define FP_TX 6
#define STATUS_PIN 1
#define WAKEUP_PIN 2
#define SLEEP_INTERVAL_S 30

#define FINGERPRINT_FOUND 0
#define FINGERPRINT_NOT_FOUND -1
#define FINGERPRINT_TIMEOUT -2
#define FINGERPRINT_ERROR -3

Adafruit_BME280 bme;
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
RTC_DATA_ATTR int timer_wakeup_count = 0;

// =============== HELFERFUNKTIONEN ===============

void initEspNow() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
}

// GEÄNDERT: Neue Schwellenwerte für den Tor-Status
uint8_t calculateTorStatus(float angle) {
  if (angle > 170.0) {
    return 0; // Geschlossen
  } else if (angle >= 150.0) {
    return 1; // Teilweise offen
  } else {
    return 2; // Offen
  }
}

int getFingerprint() {
  finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 200, 0x02, 0);
  Serial.println("Sensor ready, waiting for finger... (10s timeout)");
  delay(3000);
  unsigned long startTime = millis();
  while (millis() - startTime < 7000) {
    if (finger.getImage() == FINGERPRINT_OK) {
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0); 
      uint8_t p = finger.image2Tz();
      if (p != FINGERPRINT_OK) return FINGERPRINT_ERROR;
      p = finger.fingerSearch();
      if (p != FINGERPRINT_OK) return FINGERPRINT_NOT_FOUND;
      return finger.fingerID;
    }
    delay(50);
  }
  finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
  Serial.println("Timeout: No finger was placed on the sensor.");
  return FINGERPRINT_TIMEOUT;
}

float getNormalizedZAngle() {
  sensors_event_t event;
  accel.getEvent(&event);
  float raw_angle = atan2(event.acceleration.y, event.acceleration.x) * 180 / PI;
  if (raw_angle < 0) {
    raw_angle += 360.0;
  }
  return raw_angle;
}

void readAndPrintSensorData() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float z_angle = getNormalizedZAngle();

  if (isnan(temp) || isnan(hum) || isnan(pres)) { Serial.println("BME280 read failed!"); return; }
  Serial.println("--- Sensor Values ---");
  Serial.printf("Temperature: %.2f *C\n", temp);
  Serial.printf("Humidity:    %.2f %%\n", hum);
  Serial.printf("Pressure:    %.2f hPa\n", pres);
  Serial.printf("Z-Axis Angle (Yaw, 0-360): %.2f degrees\n", z_angle);
  Serial.println("---------------------");
}

void printSystemTime() {
  uint32_t total_seconds = timer_wakeup_count * SLEEP_INTERVAL_S;
  int hours = total_seconds / 3600;
  int minutes = (total_seconds % 3600) / 60;
  int seconds = total_seconds % 60;
  Serial.printf("System Uptime: %02d:%02d:%02d\n", hours, minutes, seconds);
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- Fingerprint Low-Power Project (Final) ---");
  initEspNow();
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x76) || !accel.begin(0x53)) {
    Serial.println("I2C sensor error!"); while(1) delay(10);
  }
  accel.setRange(ADXL345_RANGE_2_G);
  pinMode(STATUS_PIN, OUTPUT);
  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(STATUS_PIN, LOW);
  delay(1500);
  mySerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
  if (finger.verifyPassword()) {
      Serial.println("Found fingerprint sensor!");
  } else {
      Serial.println("Did not find fingerprint sensor!");
  }

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: {
      timer_wakeup_count++;
      Serial.println("Wakeup Cause: Timer.");
      printSystemTime();
      readAndPrintSensorData();

      float z_angle = getNormalizedZAngle();
      SensorMessage sensorData;
      sensorData.temperature = bme.readTemperature();
      sensorData.humidity = bme.readHumidity();
      sensorData.torStatus = calculateTorStatus(z_angle);
      double temp_c = sensorData.temperature;
      double rel_hum = sensorData.humidity;
      sensorData.absoluteHumidity = (6.112 * pow(2.71828, (17.67 * temp_c) / (temp_c + 243.5)) * rel_hum * 2.1674) / (273.15 + temp_c);
      esp_now_send(receiverMac, (uint8_t *) &sensorData, sizeof(sensorData));
      Serial.println("SensorMessage sent via ESP-NOW.");
      break;
    }
    
    case ESP_SLEEP_WAKEUP_GPIO: {
      Serial.println("Wakeup Cause: Fingerprint Sensor Touch.");
      int fingerID = getFingerprint();
      
      if (fingerID >= FINGERPRINT_FOUND) {
        Serial.printf("Fingerprint recognized! ID #%d\n", fingerID);
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x04, 0); delay(3000);
      } else { 
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x01, 0); delay(3000);
      }
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
      
      readAndPrintSensorData();
      
      FingerEvent fingerData;
      fingerData.fingerID = (fingerID >= FINGERPRINT_FOUND) ? fingerID : 0;
      fingerData.confidence = finger.confidence;
      float z_angle = getNormalizedZAngle();
      fingerData.torStatus = calculateTorStatus(z_angle);
      
      esp_now_send(receiverMac, (uint8_t *) &fingerData, sizeof(fingerData));
      Serial.println("FingerEvent sent via ESP-NOW.");
      break;
    }

    default:
      Serial.println("Wakeup Cause: Initial Boot (Power-On).");
      timer_wakeup_count = 0;
      break;
  }
  
  delay(1000); 
  Serial.println("\nConfiguring for next sleep cycle...");
  
  Esp32C3_DeepSleep::beginTimerWakeup(SLEEP_INTERVAL_S * 1000000ULL);
  Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false);
  
  digitalWrite(STATUS_PIN, HIGH);
  Esp32C3_DeepSleep::holdGPIO(STATUS_PIN);
  
  Serial.println("Entering deep sleep now...");
  
  WiFi.mode(WIFI_OFF);
  Serial.flush();
  
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}