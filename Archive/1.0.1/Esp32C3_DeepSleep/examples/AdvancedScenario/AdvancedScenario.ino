#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>

// --- Configuration ---
#define STATUS_PIN 1
#define WAKEUP_PIN 2
#define SLEEP_INTERVAL_S 30

RTC_DATA_ATTR int timer_wakeup_count = 0;

void printSystemTime() {
  uint32_t total_seconds = timer_wakeup_count * SLEEP_INTERVAL_S;
  int hours = total_seconds / 3600;
  int minutes = (total_seconds % 3600) / 60;
  int seconds = total_seconds % 60;
  Serial.printf("System Uptime: %02d:%02d:%02d\n", hours, minutes, seconds);
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Feste Pause am Anfang, damit der Monitor sicher verbunden ist
  Serial.println("\n--- Advanced Deep Sleep Scenario (Optimized Output) ---");

  pinMode(STATUS_PIN, OUTPUT);

  Esp32C3_DeepSleep::releaseAllHolds();
  digitalWrite(STATUS_PIN, LOW);
  Serial.printf("State: Awake. Pin %d is LOW.\n", STATUS_PIN);
  delay(500); // Kurze Pause, um den "Wach"-Zustand zu sehen

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      timer_wakeup_count++;
      Serial.println("Wakeup Cause: Timer.");
      printSystemTime();
      break;
    
    case ESP_SLEEP_WAKEUP_GPIO:
      Serial.println("Wakeup Cause: GPIO Pin.");
      Serial.println("Manual wakeup triggered. Time will not be advanced.");
      printSystemTime();
      break;

    default:
      Serial.println("Wakeup Cause: Initial Boot (Power-On).");
      timer_wakeup_count = 0;
      break;
  }
  
  // Längere Pause, damit du die Ausgabe in Ruhe lesen kannst
  delay(3000); 

  Serial.println("\nConfiguring for next sleep cycle...");
  delay(100); // Mini-Pause
  
  Esp32C3_DeepSleep::beginTimerWakeup(SLEEP_INTERVAL_S * 1000000ULL);
  Serial.printf("- Timer wakeup is set for %d seconds.\n", SLEEP_INTERVAL_S);
  delay(100);

  Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false);
  Serial.printf("- Pin %d is a wakeup source.\n", WAKEUP_PIN);
  delay(100);

  digitalWrite(STATUS_PIN, HIGH);
  Esp32C3_DeepSleep::holdGPIO(STATUS_PIN);
  Serial.printf("- Pin %d will be held HIGH during sleep.\n", STATUS_PIN);
  delay(100);
  
  Serial.println("\nEntering deep sleep now...");
  
  // **WICHTIG:** Warte, bis alle seriellen Nachrichten gesendet wurden.
  Serial.flush();

  // Falls du I2C oder SPI nutzt:
  // Wire.end();
  // SPI.end();
  
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // Never reached
}