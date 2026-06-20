// =====================================================================================
// GarageAussenSenderESPC3-Lib (code1C) - V9.0 (No LED / Final Clean)
// -------------------------------------------------------------------------------------
// Status: PRODUKTIV
// Hardware: 
// - Waveshare ESP32-C3-Zero (RGB LED entfernt für <1mA Sleep)
// - ADXL345 (90° gedreht, Y/Z-Achse)
// - BME280, Fingerprint
//
// Änderungen zu V8.1:
// - Alle NeoPixel/LED Befehle entfernt (da LED abgelötet).
// - Code bereinigt und kommentiert.
// =====================================================================================

#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Adafruit_BME280.h>
#include <Adafruit_Fingerprint.h>
// #include <Adafruit_NeoPixel.h> // Entfernt
#include "secrets.h"
#include "adxl_manager.h"

// ======================= STEUERUNG =======================
#define DEBUG_MODE                  true
#define NORMAL_SLEEP_INTERVAL_S     (15 * 60)
#define POST_EVENT_SLEEP_INTERVAL_S 30

// Empfindlichkeit für Bewegung (AC-Coupled)
#define ADXL_ACTIVITY_THRESHOLD     20        

// Winkel-Grenzen (Y/Z Achse)
#define TOR_WINKEL_GESCHLOSSEN_MIN  180.0      
#define TOR_WINKEL_HALBOFFEN_MIN    165.0       
#define BATTERY_WARNING_VOLTAGE     3.4

// ============= SPANNUNGSTEILER =============
#define BATTERY_PIN                 1          
// ANPASSUNG: User verwendet 2x 330k Widerstände
#define R1_VALUE                    330000.0   
#define R2_VALUE                    330000.0   
#define VOLTAGE_OFFSET              0.0        

// Korrekturwerte BME280
#define BME_TEMP_OFFSET_C           0.85
#define BME_HUM_OFFSET_PERCENT      1.73

// Nachrichtentypen
#define SENSOR_MESSAGE_TYPE_NORMAL        0
#define SENSOR_MESSAGE_TYPE_CONTROL_UPDATE 1

// ============== ESP-NOW =============
#define ESPNOW_CHANNEL 6

// ============= PINS =============
#define I2C_SDA 7
#define I2C_SCL 6
#define FP_RX 4
#define FP_TX 3
#define FP_POWER 8
#define FP_WAKEUP_PIN 5
#define GARAGE_FINGER_ACTION_ID 3250
// LED PINS entfernt

#define FINGERPRINT_FOUND 0
#define FINGERPRINT_NOT_FOUND -1
#define FP_TIMEOUT -2
#define FP_ERROR -3

// Datenstrukturen (mit Packing für binäre Kompatibilität)
struct __attribute__((packed)) SensorMessage {
  uint8_t type;
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
  uint8_t wakeupCause;
  uint32_t messageCounter; 
};

struct __attribute__((packed)) FingerEvent {
  uint8_t type = 99;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
  uint32_t messageCounter; 
};

// Globale Objekte
Adafruit_BME280 bme;
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
// NeoPixel Objekt entfernt

// Zustands-Variablen
bool adxlFound = false; 

// RTC-Variablen
enum State { NORMAL_CYCLE, AWAITING_1_MIN_CHECK };
RTC_DATA_ATTR State deviceState = NORMAL_CYCLE;
RTC_DATA_ATTR int timer_wakeup_count = 0;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR uint32_t messageCounter = 0;

// =============== HELFERFUNKTIONEN =================

float readBatteryVoltage() {
  uint32_t raw_mv = analogReadMilliVolts(BATTERY_PIN);
  float voltage = (raw_mv / 1000.0) * ((R1_VALUE + R2_VALUE) / R2_VALUE);
  return voltage + VOLTAGE_OFFSET;
}

void initEspNow() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_set_pmk((const uint8_t *)pmk_key_str);
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMac, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = true;
    esp_now_add_peer(&peerInfo);
  }
}

uint8_t calculateTorStatus(float angle) {
  if (angle > TOR_WINKEL_GESCHLOSSEN_MIN) {
    return 0; // Geschlossen
  } else if (angle >= TOR_WINKEL_HALBOFFEN_MIN) {
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
  if (!adxlFound) return 0.0;

  accel.setRange(ADXL345_RANGE_2_G);
  sensors_event_t event;
  accel.getEvent(&event);

  float raw_angle = atan2(event.acceleration.z, event.acceleration.y) * 180 / PI;
  raw_angle += 180.0;
  
  if (raw_angle >= 360.0) raw_angle -= 360.0;
  if (raw_angle < 0) raw_angle += 360.0;
  
  return raw_angle;
}

void readAndPrintSensorData() {
  delay(600);
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  temp += BME_TEMP_OFFSET_C;
  hum += BME_HUM_OFFSET_PERCENT;
  float pres = bme.readPressure() / 100.0F;
  float z_angle = getNormalizedZAngle();
  float vbat = readBatteryVoltage(); 

  if (isnan(temp) || isnan(hum) || isnan(pres)) {
    #if DEBUG_MODE
    Serial.println("BME280 read failed!");
    #endif
    return;
  }
  #if DEBUG_MODE
  uint8_t status = calculateTorStatus(z_angle);
  String statusStr = (status == 0) ? "ZU" : (status == 1) ? "HALB" : "OFFEN";
  Serial.printf("[Sensors] T:%.2f, H:%.2f, Vbat:%.2f V\n", temp, hum, vbat);
  Serial.printf("[Winkel]  %.1f Grad -> Status: %s\n", z_angle, statusStr.c_str());
  
  if (vbat < BATTERY_WARNING_VOLTAGE) {
    Serial.println("Akku niedrig! Bitte laden.");
  }
  #endif
}

void sendSensorData(uint8_t messageType, uint8_t cause) {
  delay(200);
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  temp += BME_TEMP_OFFSET_C;
  hum += BME_HUM_OFFSET_PERCENT;
  float z_angle = getNormalizedZAngle();
  float vbat = readBatteryVoltage();
  float current_mA = 0.0; 

  if (isnan(temp) || isnan(hum)) return;

  SensorMessage sensorData;
  messageCounter++;
  sensorData.messageCounter = messageCounter;
  sensorData.type = messageType;
  sensorData.temperature = temp;
  sensorData.humidity = hum;
  sensorData.torStatus = calculateTorStatus(z_angle);
  double temp_c = sensorData.temperature;
  double rel_hum = sensorData.humidity;
  sensorData.absoluteHumidity = (6.112 * pow(2.71828, (17.67 * temp_c) / (temp_c + 243.5)) * rel_hum * 2.1674) / (273.15 + temp_c);
  sensorData.batteryVoltage = vbat;
  sensorData.batteryCurrent = current_mA;
  sensorData.wakeupCause = cause;

  #if DEBUG_MODE
  Serial.printf("[SEND] Sensor: T=%.1f, Tor=%d (%.1f deg), Vbat=%.2f V, Cause=%d, Count=%u\n",
                sensorData.temperature, sensorData.torStatus, z_angle, vbat, sensorData.wakeupCause, sensorData.messageCounter);
  #endif
  esp_now_send(receiverMac, (uint8_t *) &sensorData, sizeof(sensorData));
}

void sendFingerprintData(int fingerID, uint8_t confidence, uint8_t torStatus, float currentAngle) {
  FingerEvent fingerData;
  messageCounter++;
  fingerData.messageCounter = messageCounter;
  fingerData.type = 99;
  fingerData.fingerID = (fingerID >= FINGERPRINT_FOUND) ? fingerID : 0;
  fingerData.confidence = confidence;
  fingerData.actionID = GARAGE_FINGER_ACTION_ID;
  fingerData.torStatus = torStatus;
  
  fingerData.batteryVoltage = readBatteryVoltage();
  fingerData.batteryCurrent = 0.0;

  #if DEBUG_MODE
  Serial.printf("[SEND] Finger: ID=%d, Tor=%d (Winkel: %.1f deg), Vbat=%.2f V, Count=%u\n",
                fingerData.fingerID, fingerData.torStatus, currentAngle,
                fingerData.batteryVoltage, fingerData.messageCounter);
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


void setup() {
  setCpuFrequencyMhz(80);

  #if DEBUG_MODE
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- Fingerprint Low-Power Project (V9.0 Final Clean) ---");
  Serial.println("Boot-Nummer: " + String(++bootCount));
  #endif

  pinMode(BATTERY_PIN, INPUT);
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  pinMode(FP_POWER, OUTPUT);
  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(FP_POWER, LOW);
  delay(600);

  initEspNow();
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!bme.begin(0x76)) {
    #if DEBUG_MODE
    Serial.println("BME280 nicht gefunden!");
    #endif
    while(1) delay(10);
  }

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
  
  // LED Initialisierung entfernt
  
  adxlFound = configureADXL345(ADXL_ACTIVITY_THRESHOLD);

  delay(100);

  uint32_t sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
  bool enableAdxlWakeup = true;

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();
  uint64_t gpio_wakeup_status = esp_sleep_get_gpio_wakeup_status();

  if (adxlFound) {
    clearADXLInterrupt();
  }

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: {
      if (deviceState == AWAITING_1_MIN_CHECK) {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: 1-Minute Status Check.");
        readAndPrintSensorData();
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_CONTROL_UPDATE, 1);
        deviceState = NORMAL_CYCLE;
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
        enableAdxlWakeup = true;
      } else {
        #if DEBUG_MODE
        timer_wakeup_count++;
        Serial.println("Wakeup Cause: Periodic Timer.");
        printSystemTime();
        readAndPrintSensorData();
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL, 1);
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
        enableAdxlWakeup = true;
      }
      break;
    }

    case ESP_SLEEP_WAKEUP_GPIO: {
      enableAdxlWakeup = false;
      deviceState = AWAITING_1_MIN_CHECK;
      sleep_duration_s = POST_EVENT_SLEEP_INTERVAL_S;

      if (gpio_wakeup_status & (1ULL << FP_WAKEUP_PIN)) {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: Fingerprint Sensor Touch.");
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL, 2);
        int fingerID = getFingerprint();

        if (fingerID < FINGERPRINT_FOUND) {
          #if DEBUG_MODE
          Serial.printf("Fingerprint failed or timed out. Code: %d\n", fingerID);
          #endif
          finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x01, 0);
          delay(1000);
          finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
          sendSensorData(SENSOR_MESSAGE_TYPE_CONTROL_UPDATE, 2);
        } else {
          #if DEBUG_MODE
          Serial.printf("Fingerprint recognized! ID #%d\n", fingerID);
          #endif
          finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x04, 0);
          float currentAngle = getNormalizedZAngle();
          sendFingerprintData(fingerID, finger.confidence, calculateTorStatus(currentAngle), currentAngle);
          delay(1000);
          finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
        }
      } else if (gpio_wakeup_status & (1ULL << WAKEUP_PIN)) {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: Motion Detected (ADXL345).");
        #endif
        
        // LED Anzeige entfernt
        delay(50);

        sendSensorData(SENSOR_MESSAGE_TYPE_CONTROL_UPDATE, 3);
      } else {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: Unknown GPIO Interrupt.");
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL, 0);
      }
      break;
    }

    default: {
      #if DEBUG_MODE
      Serial.println("Wakeup Cause: Initial Boot (Power-On).");
      #endif
      timer_wakeup_count = 0;
      deviceState = NORMAL_CYCLE;
      sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL, 0);
      sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      enableAdxlWakeup = true;
      break;
    }
  }

  Esp32C3_DeepSleep::beginTimerWakeup(sleep_duration_s * 1000000ULL);
  Esp32C3_DeepSleep::addWakeupPin(FP_WAKEUP_PIN, false);

  if (enableAdxlWakeup && adxlFound) {
    #if DEBUG_MODE
    Serial.println("ADXL-Wakeup wird für nächsten Zyklus aktiviert.");
    #endif
    setupADXLWakeup();
  } else {
    #if DEBUG_MODE
    Serial.println("ADXL-Wakeup wird für nächsten Zyklus deaktiviert.");
    #endif
  }

  #if DEBUG_MODE
  Serial.printf("Entering deep sleep for %d seconds...\n", sleep_duration_s);
  Serial.flush();
  #endif
  
  if (adxlFound) {
    clearADXLInterrupt();
    delay(10); 
  }

  digitalWrite(FP_POWER, HIGH);
  Esp32C3_DeepSleep::holdGPIO(FP_POWER);

  WiFi.mode(WIFI_OFF);
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}