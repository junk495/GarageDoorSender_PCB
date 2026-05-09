// =====================================================================================
// GarageAussenSenderESPC3-Lib (code1C)
// -------------------------------------------------------------------------------------
// Letzte Änderung: 20. Juni 2025, 21:55
// Hardware:      Waveshare ESP32-C3-Zero
// Funktion:      Ein batteriebetriebener Außensensor für eine Garage.
// Logik:
// - Sendet Sensor- und Torstatusdaten alle 15 Minuten.
// - Bei einem Fingerabdruck-Ereignis wird der Status sofort und nach einer
//   weiteren Minute zur Kontrolle erneut gesendet.
// - Nutzt die Esp32C3_DeepSleep.h Bibliothek zur Steuerung des Tiefschlafs.
// =====================================================================================

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

// ======================= STEUERUNG =======================
#define DEBUG_MODE      true// Monitor-Ausgaben & Debug-Delays aktivieren/deaktivieren

// ===== Energie-Einstellungen =====
// CPU-Frequenz in MHz. Niedrigere Werte sparen im Wachzustand Strom.
// Mögliche Werte für ESP32-C3: 160, 80, 40, 20, 10.
// 80MHz ist ein guter Kompromiss aus Geschwindigkeit und Ersparnis.
#define CPU_FREQUENCY_MHZ 80

// Sendeleistung des WLAN-Moduls (ESP-NOW).
// Skala: 8 (sehr niedrig, ca. +2dBm) bis 84 (maximal, ca. +20dBm).
// Ein höherer Wert erhöht die Reichweite, verbraucht aber mehr Strom.
// 70 ist ein starker, aber bereits leicht reduzierter Wert für den Start.
#define WIFI_TX_POWER     70

// Zeitliche Taktung
#define NORMAL_SLEEP_INTERVAL_S (15 * 60) // 15 Minuten
#define POST_EVENT_SLEEP_INTERVAL_S 60    // 60 Sekunden

// Schwellenwerte für Torstatus-Erkennung
#define TOR_WINKEL_GESCHLOSSEN 170.0
#define TOR_WINKEL_HALBOFFEN   140.0
// =========================================================

// ============== ESP-NOW KONFIGURATION ==============
#define ESPNOW_CHANNEL 6
uint8_t receiverMac[] = {0xB0, 0xB2, 0x1C, 0x96, 0xA4, 0x88};

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
  uint16_t actionID; 
  uint8_t torStatus;
};

// ============= SENSOR & PROJEKT KONFIGURATION =============
#define I2C_SDA 7
#define I2C_SCL 6
#define FP_RX 3
#define FP_TX 4
#define FP_POWER 8 
#define WAKEUP_PIN 5
#define GARAGE_FINGER_ACTION_ID 3250

#define FINGERPRINT_FOUND 0
#define FINGERPRINT_NOT_FOUND -1
#define FP_TIMEOUT -2
#define FP_ERROR -3   

Adafruit_BME280 bme;
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Zustandsvariable, die den Deep Sleep überlebt
enum State {
  NORMAL_CYCLE,
  AWAITING_1_MIN_CHECK
};
RTC_DATA_ATTR State deviceState = NORMAL_CYCLE;
RTC_DATA_ATTR int timer_wakeup_count = 0;

// =============== HELFERFUNKTIONEN ===============

void initEspNow() {
  WiFi.mode(WIFI_STA);

  // Verwende die zentrale Einstellung für die Sendeleistung
  esp_wifi_set_tx_power(WIFI_TX_POWER); 
  
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
}

uint8_t calculateTorStatus(float angle) {
  if (angle > TOR_WINKEL_GESCHLOSSEN) {
    return 0; // Geschlossen
  } else if (angle >= TOR_WINKEL_HALBOFFEN) {
    return 1; // Teilweise offen
  } else {
    return 2; // Offen
  }
}

int getFingerprint() {
  finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 200, 0x02, 0);
  #if DEBUG_MODE
  Serial.println("Sensor ready, waiting for finger... (7s timeout)");
  #endif
  delay(700);
  unsigned long startTime = millis();
  while (millis() - startTime < 7000) {
    if (finger.getImage() == FINGERPRINT_OK) {
      uint8_t p = finger.image2Tz();
      if (p != FINGERPRINT_OK) return FP_ERROR;
      p = finger.fingerSearch();
      if (p != FINGERPRINT_OK) return FINGERPRINT_NOT_FOUND;
      return finger.fingerID;
    }
    delay(50);
  }
  finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
  #if DEBUG_MODE
  Serial.println("Timeout: No finger was placed on the sensor.");
  #endif
  return FP_TIMEOUT; 
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

  if (isnan(temp) || isnan(hum) || isnan(pres)) { 
    #if DEBUG_MODE
    Serial.println("BME280 read failed!"); 
    #endif
    return; 
  }
  #if DEBUG_MODE
  Serial.printf("[Sensors] T:%.2f, H:%.2f, P:%.2f, D:%.2f\n", temp, hum, pres, z_angle);
  #endif
}

void sendSensorData() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float z_angle = getNormalizedZAngle();

  if (isnan(temp) || isnan(hum)) { 
      #if DEBUG_MODE
      Serial.println("BME280 read failed!"); 
      #endif
      return; 
  }

  SensorMessage sensorData;
  sensorData.temperature = temp;
  sensorData.humidity = hum;
  sensorData.torStatus = calculateTorStatus(z_angle);
  double temp_c = sensorData.temperature;
  double rel_hum = sensorData.humidity;
  sensorData.absoluteHumidity = (6.112 * pow(2.71828, (17.67 * temp_c) / (temp_c + 243.5)) * rel_hum * 2.1674) / (273.15 + temp_c);
  
  #if DEBUG_MODE
  Serial.printf("[SEND] Sensor: T=%.1f, H=%.1f, Tor=%d (%.1f deg)\n", sensorData.temperature, sensorData.humidity, sensorData.torStatus, z_angle);
  #endif
  esp_now_send(receiverMac, (uint8_t *) &sensorData, sizeof(sensorData));
}

void sendFingerprintData(int fingerID, uint8_t confidence, uint8_t torStatus, float currentAngle) {
  FingerEvent fingerData;
  fingerData.fingerID = (fingerID >= FINGERPRINT_FOUND) ? fingerID : 0;
  fingerData.confidence = confidence;
  fingerData.actionID = GARAGE_FINGER_ACTION_ID;
  fingerData.torStatus = torStatus;
  
  #if DEBUG_MODE
  Serial.printf("[SEND] Finger: ID=%d, Tor=%d (%.1f deg), ActionID=%d\n", fingerData.fingerID, fingerData.torStatus, currentAngle, fingerData.actionID);
  #endif
  esp_now_send(receiverMac, (uint8_t *) &fingerData, sizeof(fingerData));
}

void printSystemTime() {
  uint32_t total_seconds = timer_wakeup_count * NORMAL_SLEEP_INTERVAL_S;
  int hours = total_seconds / 3600;
  int minutes = (total_seconds % 3600) / 60;
  int seconds = total_seconds % 60;
  #if DEBUG_MODE
  Serial.printf("System Uptime: %02d:%02d:%02d\n", hours, minutes, seconds);
  #endif
}


// ======================== SETUP ========================
void setup() {
  // Verwende die zentrale Einstellung für die CPU-Frequenz
  setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);

  #if DEBUG_MODE
  Serial.begin(115200);
  delay(200); 
  Serial.println("\n--- Fingerprint Low-Power Project (Final) ---");
  #endif

  pinMode(FP_POWER, OUTPUT);
  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(FP_POWER, LOW);
  delay(200); 

  initEspNow();
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x76) || !accel.begin(0x53)) {
    #if DEBUG_MODE
    Serial.println("I2C sensor error!");
    #endif 
    while(1) delay(10);
  }
  accel.setRange(ADXL345_RANGE_2_G);
  
  mySerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
  if (finger.verifyPassword()) {
      #if DEBUG_MODE
      Serial.println("Found fingerprint sensor!");
      #endif
  } else {
      #if DEBUG_MODE
      Serial.println("Did not find fingerprint sensor!");
      #endif
  }

  uint32_t sleep_duration_s;

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: {
      if (deviceState == AWAITING_1_MIN_CHECK) {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: 1-Minute Status Check.");
        readAndPrintSensorData();
        #endif
        sendSensorData();
        deviceState = NORMAL_CYCLE;
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      } else {
        #if DEBUG_MODE
        timer_wakeup_count++;
        Serial.println("Wakeup Cause: Periodic Timer.");
        printSystemTime();
        readAndPrintSensorData();
        #endif
        sendSensorData();
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      }
      break;
    }
    
    case ESP_SLEEP_WAKEUP_GPIO: {
      #if DEBUG_MODE
      Serial.println("Wakeup Cause: Fingerprint Sensor Touch.");
      #endif
      int fingerID = getFingerprint();

      if (fingerID < FINGERPRINT_FOUND) {
        #if DEBUG_MODE
        Serial.printf("Fingerprint failed or timed out. Code: %d\n", fingerID);
        #endif
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x01, 0); 
        delay(1000);
        finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);

        deviceState = AWAITING_1_MIN_CHECK;
        sleep_duration_s = POST_EVENT_SLEEP_INTERVAL_S;
        break; 
      }

      #if DEBUG_MODE
      Serial.printf("Fingerprint recognized! ID #%d\n", fingerID);
      #endif
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x04, 0); 
      float currentAngle = getNormalizedZAngle();
      sendFingerprintData(fingerID, finger.confidence, calculateTorStatus(currentAngle), currentAngle);
      delay(1000);
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
      
      deviceState = AWAITING_1_MIN_CHECK;
      sleep_duration_s = POST_EVENT_SLEEP_INTERVAL_S;
      break;
    }

    default:
      #if DEBUG_MODE
      Serial.println("Wakeup Cause: Initial Boot (Power-On).");
      #endif
      timer_wakeup_count = 0;
      deviceState = NORMAL_CYCLE;
      sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      break;
  }
  
  #if DEBUG_MODE
  delay(1000); 
  Serial.println("\nConfiguring for next sleep cycle...");
  #endif
  
  Esp32C3_DeepSleep::beginTimerWakeup(sleep_duration_s * 1000000ULL);
  Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false);
  
  digitalWrite(FP_POWER, HIGH);
  Esp32C3_DeepSleep::holdGPIO(FP_POWER);
  
  #if DEBUG_MODE
  Serial.println("Entering deep sleep now...");
  Serial.flush();
  #endif
  
  WiFi.mode(WIFI_OFF);
  
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}
