// =====================================================================================
// GarageAussenSenderESPC3-Lib (code1C) - V1.7 (Fix für NeoPixel/UART-Konflikt)
// -------------------------------------------------------------------------------------
// Letzte Änderung: 4. Juli 2025 (Initialisierungs-Reihenfolge zur Behebung des
//                                 Hardware-Konflikts zwischen NeoPixel und Fingerprint-Sensor geändert)
// Hardware:        Waveshare ESP32-C3-Zero
// Funktion:        Ein batteriebetriebener Außensensor für eine Garage.
// Logik:
// - Sendet alle Daten sicher verschlüsselt via ESP-NOW.
// - Sendet Sensor-, Torstatus- und Akkudaten alle 15 Minuten (NORMAL_SLEEP_INTERVAL_S).
// - Bei einem Fingerabdruck-Ereignis wird ein FingerEvent sofort gesendet.
// - Bei einer Bewegungserkennung (ADXL345) wird eine sofortige Sensormeldung gesendet
//   und die eingebaute RGB-LED blitzt kurz rot auf.
// - Nach einem Ereignis (Finger oder Bewegung) wird der ADXL-Wakeup für 30s
//   deaktiviert, um Endlosschleifen bei Dauer-Bewegung zu verhindern.
// =====================================================================================

#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA219.h>
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Adafruit_BME280.h>
#include <Adafruit_Fingerprint.h>
#include <Adafruit_NeoPixel.h> // Bibliothek für die RGB-LED
#include "secrets.h"
#include "adxl_manager.h"

// ======================= STEUERUNG =======================
#define DEBUG_MODE                  true
#define NORMAL_SLEEP_INTERVAL_S     (15 * 60)
#define POST_EVENT_SLEEP_INTERVAL_S 30
#define TOR_WINKEL_GESCHLOSSEN      170.0
#define TOR_WINKEL_HALBOFFEN        140.0
#define BATTERY_WARNING_VOLTAGE     3.4

// Korrekturwerte für BME280-Sensoren
#define BME_TEMP_OFFSET_C           0.85
#define BME_HUM_OFFSET_PERCENT      1.73

// Typen für Nachrichten
#define SENSOR_MESSAGE_TYPE_NORMAL        0
#define SENSOR_MESSAGE_TYPE_CONTROL_UPDATE 1

// ============== ESP-NOW KONFIGURATION ==============
#define ESPNOW_CHANNEL 6
uint8_t receiverMac[] = {0xB0, 0xB2, 0x1C, 0x96, 0xA4, 0x88};

// ============= SENSOR & PROJEKT KONFIGURATION =============
#define I2C_SDA 7
#define I2C_SCL 6
#define FP_RX 3
#define FP_TX 4
#define FP_POWER 8
#define FP_WAKEUP_PIN 5
#define GARAGE_FINGER_ACTION_ID 3250
#define LED_PIN 10      // Pin der WS2812 RGB LED
#define LED_COUNT 1     // Anzahl der LEDs

#define FINGERPRINT_FOUND 0
#define FINGERPRINT_NOT_FOUND -1
#define FP_TIMEOUT -2
#define FP_ERROR -3

// Datenstrukturen
struct SensorMessage {
  uint8_t type;
  float temperature;
  float humidity;
  float absoluteHumidity;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
};

struct FingerEvent {
  uint8_t type = 99;
  uint8_t fingerID;
  uint8_t confidence;
  uint16_t actionID;
  uint8_t torStatus;
  float batteryVoltage;
  float batteryCurrent;
};

// Globale Objekte
Adafruit_BME280 bme;
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
Adafruit_INA219 ina219;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800); // LED-Objekt

enum State {
  NORMAL_CYCLE,
  AWAITING_1_MIN_CHECK
};
RTC_DATA_ATTR State deviceState = NORMAL_CYCLE;
RTC_DATA_ATTR int timer_wakeup_count = 0;
RTC_DATA_ATTR int bootCount = 0;

// ... (alle Helferfunktionen bleiben exakt gleich) ...
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
  accel.setRange(ADXL345_RANGE_2_G); // Zurücksetzen für Torwinkel-Berechnung
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

  temp += BME_TEMP_OFFSET_C;
  hum += BME_HUM_OFFSET_PERCENT;

  float pres = bme.readPressure() / 100.0F;
  float z_angle = getNormalizedZAngle();
  float vbat = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  ina219.powerSave(true);

  if (isnan(temp) || isnan(hum) || isnan(pres)) {
    #if DEBUG_MODE
    Serial.println("BME280 read failed!");
    #endif
    return;
  }
  #if DEBUG_MODE
  Serial.printf("[Sensors] T:%.2f, H:%.2f, P:%.2f, D:%.2f, Vbat:%.2f V, I:%.2f mA\n",
                temp, hum, pres, z_angle, vbat, current_mA);
  if (vbat < BATTERY_WARNING_VOLTAGE) {
    Serial.println("Akku niedrig! Bitte laden.");
  }
  #endif
}

void sendSensorData(uint8_t messageType) {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();

  temp += BME_TEMP_OFFSET_C;
  hum += BME_HUM_OFFSET_PERCENT;

  float z_angle = getNormalizedZAngle();
  float vbat = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  ina219.powerSave(true);

  if (isnan(temp) || isnan(hum)) {
    #if DEBUG_MODE
    Serial.println("BME280 read failed!");
    #endif
    return;
  }

  SensorMessage sensorData;
  sensorData.type = messageType;
  sensorData.temperature = temp;
  sensorData.humidity = hum;
  sensorData.torStatus = calculateTorStatus(z_angle);
  double temp_c = sensorData.temperature;
  double rel_hum = sensorData.humidity;
  sensorData.absoluteHumidity = (6.112 * pow(2.71828, (17.67 * temp_c) / (temp_c + 243.5)) * rel_hum * 2.1674) / (273.15 + temp_c);
  sensorData.batteryVoltage = vbat;
  sensorData.batteryCurrent = current_mA;

  #if DEBUG_MODE
  Serial.printf("[SEND] Sensor: T=%.1f, H=%.1f, Tor=%d (%.1f deg), Vbat=%.2f V, I=%.2f mA, Type=%d\n",
                sensorData.temperature, sensorData.humidity, sensorData.torStatus, z_angle, vbat, current_mA, sensorData.type);
  if (vbat < BATTERY_WARNING_VOLTAGE) {
    Serial.println("Akku niedrig! Bitte laden.");
  }
  #endif
  esp_now_send(receiverMac, (uint8_t *) &sensorData, sizeof(sensorData));
}

void sendFingerprintData(int fingerID, uint8_t confidence, uint8_t torStatus, float currentAngle) {
  FingerEvent fingerData;
  fingerData.type = 99;
  fingerData.fingerID = (fingerID >= FINGERPRINT_FOUND) ? fingerID : 0;
  fingerData.confidence = confidence;
  fingerData.actionID = GARAGE_FINGER_ACTION_ID;
  fingerData.torStatus = torStatus;
  fingerData.batteryVoltage = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  ina219.powerSave(true);
  fingerData.batteryCurrent = current_mA;

  #if DEBUG_MODE
  Serial.printf("[SEND] Finger: ID=%d, Tor=%d (Winkel: %.1f deg), ActionID=%d, Vbat=%.2f V, I=%.2f mA\n",
                fingerData.fingerID, fingerData.torStatus, currentAngle, fingerData.actionID,
                fingerData.batteryVoltage, fingerData.batteryCurrent);
  if (fingerData.batteryVoltage < BATTERY_WARNING_VOLTAGE) {
    Serial.println("Akku niedrig! Bitte laden.");
  }
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
  Serial.println("\n--- Fingerprint Low-Power Project with ADXL Motion Detection ---");
  Serial.println("Boot-Nummer: " + String(++bootCount));
  #endif

  pinMode(FP_POWER, OUTPUT);
  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(FP_POWER, LOW);
  delay(200);

  initEspNow();
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ina219.begin()) {
    #if DEBUG_MODE
    Serial.println("INA219 nicht gefunden!");
    #endif
  }
  ina219.setCalibration_32V_2A();

  if (!bme.begin(0x76)) {
    #if DEBUG_MODE
    Serial.println("BME280 nicht gefunden!");
    #endif
    while(1) delay(10);
  }

  // ZUERST: Den Fingerprint-Sensor initialisieren, da er empfindlich auf Timing reagiert.
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

  // DANACH: Die LED initialisieren.
  strip.begin();
  strip.clear();
  strip.show();
  
  // Konfiguriere ADXL345 für Bewegungserkennung
  configureADXL345();

  delay(100);

  // ... (der Rest der setup() Funktion bleibt unverändert) ...
  uint32_t sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
  bool enableAdxlWakeup = true; // Flag zur Steuerung des ADXL-Wakeups

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();
  uint64_t gpio_wakeup_status = esp_sleep_get_gpio_wakeup_status();

  // Lösche ADXL345-Interrupt sofort nach dem Aufwachen
  clearADXLInterrupt();

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: {
      if (deviceState == AWAITING_1_MIN_CHECK) {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: 1-Minute Status Check.");
        readAndPrintSensorData();
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_CONTROL_UPDATE);
        deviceState = NORMAL_CYCLE;
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
        enableAdxlWakeup = true; // Nach Kontrolle ADXL wieder aktivieren
      } else {
        #if DEBUG_MODE
        timer_wakeup_count++;
        Serial.println("Wakeup Cause: Periodic Timer.");
        printSystemTime();
        readAndPrintSensorData();
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL);
        sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
        enableAdxlWakeup = true; // Im Normalbetrieb ist ADXL aktiv
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
        sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL);
        int fingerID = getFingerprint();

        if (fingerID < FINGERPRINT_FOUND) {
          #if DEBUG_MODE
          Serial.printf("Fingerprint failed or timed out. Code: %d\n", fingerID);
          #endif
          finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x01, 0);
          delay(1000);
          finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
          sendSensorData(SENSOR_MESSAGE_TYPE_CONTROL_UPDATE);
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
        
        // LED rot aufblitzen lassen
        strip.setPixelColor(0, strip.Color(40, 0, 0)); // Rotes Licht, moderate Helligkeit
        strip.show();
        delay(50); // Kurz warten
        strip.clear();
        strip.show();

        sendSensorData(SENSOR_MESSAGE_TYPE_CONTROL_UPDATE);
      } else {
        #if DEBUG_MODE
        Serial.println("Wakeup Cause: Unknown GPIO Interrupt.");
        #endif
        sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL);
      }
      break;
    }

    default: {
      #if DEBUG_MODE
      Serial.println("Wakeup Cause: Initial Boot (Power-On).");
      #endif
      timer_wakeup_count = 0;
      deviceState = NORMAL_CYCLE;
      sendSensorData(SENSOR_MESSAGE_TYPE_NORMAL);
      sleep_duration_s = NORMAL_SLEEP_INTERVAL_S;
      enableAdxlWakeup = true; // Beim ersten Start ADXL aktivieren
      break;
    }
  }

  // Konfiguriere Wecksignale für den nächsten Tiefschlaf
  Esp32C3_DeepSleep::beginTimerWakeup(sleep_duration_s * 1000000ULL);
  Esp32C3_DeepSleep::addWakeupPin(FP_WAKEUP_PIN, false); // Fingerprint ist IMMER aktiv

  // Aktiviere den ADXL-Wakeup nur, wenn es der Zustand erlaubt
  if (enableAdxlWakeup) {
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
  Serial.println("Konfiguriere Wecksignal und gehe in den Tiefschlaf...");
  delay(100);
  Serial.printf("Entering deep sleep for %d seconds...\n", sleep_duration_s);
  Serial.flush();
  #endif

  digitalWrite(FP_POWER, HIGH);
  Esp32C3_DeepSleep::holdGPIO(FP_POWER);

  WiFi.mode(WIFI_OFF);
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}