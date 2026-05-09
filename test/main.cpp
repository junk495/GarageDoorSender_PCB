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
#define DEBUG_MODE      true  // Monitor-Ausgaben & Debug-Delays aktivieren/deaktivieren

// NEU: Zeitliche Taktung
#define NORMAL_SLEEP_INTERVAL_S (1 * 60) // 15 Minuten
#define POST_EVENT_SLEEP_INTERVAL_S 30    // 60 Sekunden

// NEU: Schwellenwerte für Torstatus-Erkennung
#define TOR_WINKEL_GESCHLOSSEN 170.0
#define TOR_WINKEL_HALBOFFEN   150.0
// =========================================================

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
  uint16_t actionID;
  uint8_t torStatus;
};

// ============= SENSOR & PROJEKT KONFIGURATION =============
#define I2C_SDA 8
#define I2C_SCL 5
#define FP_RX 7
#define FP_TX 6
#define STATUS_PIN 1
#define WAKEUP_PIN 2
#define GARAGE_FINGER_ACTION_ID 3250

#define FINGERPRINT_FOUND 0
#define FINGERPRINT_NOT_FOUND -1
#define FINGERPRINT_TIMEOUT -2
#define FINGERPRINT_ERROR -3

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
  Serial.println("[FINGER] Warte auf Finger...");
  delay(3000); // Nur im Debug-Modus warten, um LED zu sehen
  #endif
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
  #if DEBUG_MODE
  Serial.println("[FINGER] Timeout.");
  #endif
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

void sendSensorData() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float z_angle = getNormalizedZAngle();

  if (isnan(temp) || isnan(hum)) { 
      #if DEBUG_MODE
      Serial.println("[ERROR] BME280 read failed!");
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
  Serial.printf("[SEND] Sensor: T=%.1f, H=%.1f, Tor=%d\n", sensorData.temperature, sensorData.humidity, sensorData.torStatus);
  #endif
  esp_now_send(receiverMac, (uint8_t *) &sensorData, sizeof(sensorData));
}

void sendFingerprintData(int fingerID, uint8_t confidence, uint8_t torStatus) {
  FingerEvent fingerData;
  fingerData.fingerID = (fingerID >= FINGERPRINT_FOUND) ? fingerID : 0;
  fingerData.confidence = confidence;
  fingerData.actionID = GARAGE_FINGER_ACTION_ID;
  fingerData.torStatus = torStatus;
  
  #if DEBUG_MODE
  Serial.printf("[SEND] Finger: ID=%d, Tor=%d, ActionID=%d\n", fingerData.fingerID, fingerData.torStatus, fingerData.actionID);
  #endif
  esp_now_send(receiverMac, (uint8_t *) &fingerData, sizeof(fingerData));
}


// ======================== SETUP ========================
void setup() {
  #if DEBUG_MODE
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n[SETUP] Start...");
  #endif

  initEspNow();
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x76) || !accel.begin(0x53)) {
    #if DEBUG_MODE
    Serial.println("[ERROR] I2C sensor!");
    #endif
    while(1) delay(10);
  }
  accel.setRange(ADXL345_RANGE_2_G);

  pinMode(STATUS_PIN, OUTPUT);
  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(STATUS_PIN, LOW);
  delay(1500);

  mySerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
  if (finger.verifyPassword()) {
      #if DEBUG_MODE
      Serial.println("[SETUP] Fingerprint-Sensor OK.");
      #endif
  } else {
      #if DEBUG_MODE
      Serial.println("[ERROR] Fingerprint-Sensor nicht gefunden!");
      #endif
  }

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();
  uint32_t sleep_duration_s;

  switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO: {
      #if DEBUG_MODE
      Serial.println("[WAKE] GPIO (Fingerprint)");
      #endif
      int fingerID = getFingerprint();
      
      if (fingerID >= FINGERPRINT_FOUND) {
        #if DEBUG_MODE
        Serial.printf("[FINGER] Match! ID #%d\n", fingerID);
        #endif
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x04, 0); // Grün
        sendFingerprintData(fingerID, finger.confidence, calculateTorStatus(getNormalizedZAngle()));
      } else { 
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x01, 0); // Rot
      }
      
      #if DEBUG_MODE
      delay(3000); // Nur im Debug-Modus warten, um LED zu sehen
      #endif
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);

      // Zustand für den nächsten, kurzen Schlafzyklus setzen
      deviceState = AWAITING_1_MIN_CHECK;
      sleep_duration_s = POST_EVENT_SLEEP_INTERVAL_S;
      #if DEBUG_MODE
      Serial.printf("[SLEEP] Konfiguriere für %d Sekunden Schlaf (1-Min-Check).\n", sleep_duration_s);
      #endif
      break;
    }
    
    case ESP_SLEEP_WAKEUP_TIMER: {
      if (deviceState == AWAITING_1_MIN_CHECK) {
        #if DEBUG_MODE
        Serial.println("[WAKE] Timer (1-Minuten-Kontrolle)");
        #endif
        sendSensorData();
        // Zurück zum Normalbetrieb
        deviceState = NORMAL_CYCLE;
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      } else {
        #if DEBUG_MODE
        Serial.println("[WAKE] Timer (Normaler Zyklus)");
        #endif
        sendSensorData();
        // Im Normalbetrieb bleiben
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      }
      break;
    }

    default: { // Power-On / Reset
      #if DEBUG_MODE
      Serial.println("[WAKE] Power-On / Reset");
      #endif
      deviceState = NORMAL_CYCLE;
      sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      break;
    }
  }
  
  #if DEBUG_MODE
  delay(1000); 
  Serial.printf("[SLEEP] Nächster Weckruf in %u Sekunden...\n", sleep_duration_s);
  #endif
  
  Esp32C3_DeepSleep::beginTimerWakeup(sleep_duration_s * 1000000ULL);
  Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false);
  
  digitalWrite(STATUS_PIN, HIGH);
  Esp32C3_DeepSleep::holdGPIO(STATUS_PIN);
  
  #if DEBUG_MODE
  Serial.println("[SLEEP] Gehe jetzt schlafen...");
  Serial.flush();
  #endif
  
  WiFi.mode(WIFI_OFF);
  
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}
