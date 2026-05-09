#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>

// Sensor-Bibliotheken
#include <Adafruit_BME280.h>
#include <Adafruit_ADXL345_U.h>
#include <Adafruit_Fingerprint.h>

// I2C Pins
#define I2C_SDA 8
#define I2C_SCL 5

// Fingerprint Sensor UART Pins
#define FP_RX 7
#define FP_TX 6

// NEU: Eigene Ergebnis-Codes für den Fingerprint-Scan
#define FINGERPRINT_FOUND 0       // Platzhalter, jeder positive Wert ist eine ID
#define FINGERPRINT_NOT_FOUND -1
#define FINGERPRINT_TIMEOUT -2
#define FINGERPRINT_ERROR -3

// Sensor-Objekte erstellen
Adafruit_BME280 bme;
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Allgemeine Konfiguration
#define STATUS_PIN 1
#define WAKEUP_PIN 2
#define SLEEP_INTERVAL_S 30

RTC_DATA_ATTR int timer_wakeup_count = 0;

// VÖLLIG NEUE IMPLEMENTIERUNG der getFingerprint() Funktion mit Timeout
int getFingerprint() {
  // LED blau atmen lassen, um Bereitschaft zu signalisieren
  finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 200, 0x02, 0); // 0x02 für Blau
  Serial.println("Sensor ready, waiting for finger... (10s timeout)");
  
  // Garantierte 3 Sekunden Leuchtzeit
  delay(3000);

  unsigned long startTime = millis();
  // Loop für die restlichen 7 Sekunden
  while (millis() - startTime < 7000) {
    if (finger.getImage() == FINGERPRINT_OK) {
      // Finger wurde aufgelegt!
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0); // Blaue LED aus

      uint8_t p = finger.image2Tz();
      if (p != FINGERPRINT_OK) return FINGERPRINT_ERROR;

      p = finger.fingerSearch();
      if (p != FINGERPRINT_OK) return FINGERPRINT_NOT_FOUND;
      
      // Erfolg! Gebe die gefundene ID zurück.
      return finger.fingerID;
    }
    delay(50); // Kurze Pause, um den Prozessor nicht zu blockieren
  }

  // Wenn wir hier ankommen, ist die Zeit abgelaufen
  finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0);
  Serial.println("Timeout: No finger was placed on the sensor.");
  return FINGERPRINT_TIMEOUT;
}

void readAndPrintSensorData() { /* ... (unverändert) ... */ }
void printSystemTime() { /* ... (unverändert) ... */ }

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- Fingerprint Low-Power Project v2 ---");

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x76) || !accel.begin(0x53)) {
    Serial.println("I2C sensor error!"); while(1) delay(10);
  }
  accel.setRange(ADXL345_RANGE_2_G);

  pinMode(STATUS_PIN, OUTPUT);
  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(STATUS_PIN, LOW);
  delay(500); 

  mySerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
  if (finger.verifyPassword()) {
      Serial.println("Found fingerprint sensor!");
  } else {
      Serial.println("Did not find fingerprint sensor!");
  }

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      timer_wakeup_count++;
      Serial.println("Wakeup Cause: Timer.");
      printSystemTime();
      readAndPrintSensorData();
      break;
    
    case ESP_SLEEP_WAKEUP_GPIO:
    {
      Serial.println("Wakeup Cause: Fingerprint Sensor Touch.");
      
      int fingerID = getFingerprint();
      
      // GEÄNDERT: Logik, die auf die neuen Ergebnis-Codes reagiert
      if (fingerID >= FINGERPRINT_FOUND) { // Jeder positive Wert (und 0) ist eine gültige ID
        Serial.printf("Fingerprint recognized! ID #%d\n", fingerID);
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x04, 0); // Grün AN
        delay(3000);
      } else { 
        // Behandle die verschiedenen Fehlerfälle
        switch(fingerID) {
          case FINGERPRINT_NOT_FOUND:
            Serial.println("Finger not found in database.");
            break;
          case FINGERPRINT_TIMEOUT:
            Serial.println("Scan aborted due to timeout.");
            break;
          case FINGERPRINT_ERROR:
            Serial.println("Error during fingerprint imaging.");
            break;
        }
        finger.LEDcontrol(FINGERPRINT_LED_ON, 0, 0x01, 0); // Rot AN
        delay(3000);
      }
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0); // LED nach Feedback ausschalten
      
      readAndPrintSensorData();
      break;
    }

    default:
      Serial.println("Wakeup Cause: Initial Boot (Power-On).");
      timer_wakeup_count = 0;
      break;
  }
  
  delay(3000); 

  Serial.println("\nConfiguring for next sleep cycle...");
  
  Esp32C3_DeepSleep::beginTimerWakeup(SLEEP_INTERVAL_S * 1000000ULL);
  Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false);
  
  digitalWrite(STATUS_PIN, HIGH);
  Esp32C3_DeepSleep::holdGPIO(STATUS_PIN);
  
  Serial.println("Entering deep sleep now...");
  Serial.flush();
  
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}