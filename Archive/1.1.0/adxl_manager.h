#ifndef ADXL_MANAGER_H
#define ADXL_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

// --- KONFIGURATION ---
#define SENSOR_ID 12345
#define WAKEUP_PIN GPIO_NUM_2 // GPIO-Pin für ADXL345 INT1
#define SENSITIVITY 30 // Empfindlichkeit (0=sehr empfindlich, 100=sehr unempfindlich)

// Achse für die Bewegungserkennung
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
#define AXIS X_AXIS // Standard: X-Achse, ändern zu Y_AXIS oder Z_AXIS

// --- GLOBALE VARIABLEN ---
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(SENSOR_ID);

// --- FUNKTIONEN ---

// Konfiguriert den ADXL345 für Bewegungserkennung auf der ausgewählten Achse
inline void configureADXL345() {
  if (!accel.begin()) {
    #ifdef DEBUG_MODE
    Serial.println("ADXL345 nicht gefunden! Verkabelung prüfen.");
    #endif
    while (1) delay(10); // Endlosschleife bei Fehler
  }
  
  // Setzt den Messbereich
  accel.setRange(ADXL345_RANGE_16_G);
  
  // Berechnet die Aktivitätsschwelle
  uint8_t activityThreshold = map(SENSITIVITY, 0, 100, 10, 120);
  accel.writeRegister(ADXL345_REG_THRESH_ACT, activityThreshold);
  
  // Konfiguriert die ausgewählte Achse (DC-gekoppelt)
  uint8_t act_inact_ctl;
  switch (AXIS) {
    case X_AXIS:
      act_inact_ctl = 0b11000000; // 0xC0 = X-Achse
      break;
    case Y_AXIS:
      act_inact_ctl = 0b10100000; // 0xA0 = Y-Achse
      break;
    case Z_AXIS:
      act_inact_ctl = 0b10010000; // 0x90 = Z-Achse
      break;
    default:
      act_inact_ctl = 0b11000000; // Fallback: X-Achse
      break;
  }
  accel.writeRegister(ADXL345_REG_ACT_INACT_CTL, act_inact_ctl);
  
  // Aktiviert den ACTIVITY-Interrupt
  accel.writeRegister(ADXL345_REG_INT_ENABLE, 0x10); // Bit 4 = ACTIVITY
  
  // Mappt den Interrupt auf INT1
  accel.writeRegister(ADXL345_REG_INT_MAP, 0x00);
  
  // Setzt Interrupt-Polarität auf aktiv-LOW
  uint8_t data_format = accel.readRegister(ADXL345_REG_DATA_FORMAT);
  data_format |= 0b00100000; // Bit 5 = INT_INVERT
  accel.writeRegister(ADXL345_REG_DATA_FORMAT, data_format);

  // Löscht die Interrupt-Quelle
  accel.readRegister(ADXL345_REG_INT_SOURCE);

  #ifdef DEBUG_MODE
  Serial.println("ADXL345 für Aktivitäts-Interrupt");
  #endif
}

// Initialisiert den Weck-Pin für den Tiefschlaf
inline void setupADXLWakeup() {
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
}

// Liest und löscht den Interrupt-Status des ADXL345
inline void clearADXLInterrupt() {
  accel.readRegister(ADXL345_REG_INT_SOURCE);
}

#endif // ADXL_MANAGER_H