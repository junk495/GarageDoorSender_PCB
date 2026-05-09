#ifndef ADXL_MANAGER_H
#define ADXL_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include "Esp32C3_DeepSleep.h"

// --- DEBUG-MODUS ---
#define DEBUG_MODE                  true

// --- KONFIGURATION ---
#define SENSOR_ID 12345
#define WAKEUP_PIN GPIO_NUM_2 // Wichtig: Muss an INT1 des ADXL angeschlossen sein

// --- GLOBALE VARIABLEN ---
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(SENSOR_ID);

// --- DIAGNOSEFUNKTION ---
inline void printADXLRegisters(uint8_t activityThreshold) {
  #ifdef DEBUG_MODE
    Serial.println("--- ADXL345 Register-Dump (Alle Achsen) ---");
    Serial.printf("INT_ENABLE:     0x%02X (sollte 0x10)\n", accel.readRegister(ADXL345_REG_INT_ENABLE));
    // INT_MAP ist jetzt wieder 0x00 (INT1)
    Serial.printf("INT_MAP:        0x%02X (sollte 0x00)\n", accel.readRegister(ADXL345_REG_INT_MAP));
    Serial.printf("ACT_INACT_CTL:  0x%02X (sollte 0x70)\n", accel.readRegister(ADXL345_REG_ACT_INACT_CTL));
    Serial.printf("THRESH_ACT:     0x%02X (ist %d)\n", accel.readRegister(ADXL345_REG_THRESH_ACT), activityThreshold);
    Serial.printf("DATA_FORMAT:    0x%02X (sollte 0x2B sein)\n", accel.readRegister(ADXL345_REG_DATA_FORMAT));
    Serial.println("----------------------------------------------------");
  #endif
}


// Konfiguriert den ADXL345 für den manuellen Low-Power Schlafmodus
// Return: true wenn Sensor gefunden, false wenn nicht
inline bool configureADXL345(uint8_t activityThreshold) {
  if (!accel.begin()) {
    #ifdef DEBUG_MODE
    Serial.println("WARNUNG: ADXL345 nicht gefunden! ESP startet trotzdem (ohne Motion-Feature).");
    #endif
    return false;
  }
  
  accel.writeRegister(ADXL345_REG_POWER_CTL, 0x00);
  delay(5);

  accel.setRange(ADXL345_RANGE_16_G);
  accel.setDataRate(ADXL345_DATARATE_100_HZ);

  accel.writeRegister(ADXL345_REG_THRESH_ACT, activityThreshold);

  // DC-Kopplung (Standard 0x70) - Falls du AC brauchst, hier wieder auf 0xF0 ändern
  uint8_t act_inact_ctl = 0;
  act_inact_ctl |= (1 << 6); // ACT_X enable
  act_inact_ctl |= (1 << 5); // ACT_Y enable
  act_inact_ctl |= (1 << 4); // ACT_Z enable
  accel.writeRegister(ADXL345_REG_ACT_INACT_CTL, act_inact_ctl);
  
  accel.writeRegister(ADXL345_REG_INT_ENABLE, 0x10);
  
  // WICHTIGE ÄNDERUNG: Zurück auf INT1
  // 0x00 = Bit 4 ist 0 -> Map to INT1
  accel.writeRegister(ADXL345_REG_INT_MAP, 0x00);

  uint8_t data_format = accel.readRegister(ADXL345_REG_DATA_FORMAT);
  data_format |= 0b00100000; 
  accel.writeRegister(ADXL345_REG_DATA_FORMAT, data_format);
  
  accel.readRegister(ADXL345_REG_INT_SOURCE);
  accel.writeRegister(ADXL345_REG_POWER_CTL, 0x0C);

  #ifdef DEBUG_MODE
  Serial.println("ADXL345 Config: INT1 Mapped (Clean).");
  printADXLRegisters(activityThreshold);
  #endif

  return true;
}

inline void setupADXLWakeup() {
  Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false);
}

inline void clearADXLInterrupt() {
  accel.readRegister(ADXL345_REG_INT_SOURCE);
}

#endif // ADXL_MANAGER_H