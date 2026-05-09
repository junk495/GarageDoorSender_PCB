/**
 * @file PinWakeup.ino
 * @brief Updated example for waking up using a GPIO pin.
 *
 * This sketch uses only a pin to wake up. The library will detect
 * that no timer is used and automatically power down RTC memory
 * during sleep for maximum power savings.
 *
 * Hardware Setup:
 * - A push button between GPIO 2 and GND.
 */

#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>

#define WAKEUP_PIN 2

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait for Serial Monitor to connect
  Serial.println("\n--- Pin Wakeup Example (Updated) ---");

  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();

  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("Wakeup successful! Triggered by GPIO pin.");
    delay(2000); // Wait so the message can be read
  } else {
    Serial.println("Initial boot. Configuring GPIO wakeup.");
    delay(500);
    Esp32C3_DeepSleep::addWakeupPin(WAKEUP_PIN, false); // Wake on LOW
    Serial.printf("Configuration done. GPIO %d is a wakeup source.\n", WAKEUP_PIN);
    Serial.println("--> TO TEST: Briefly connect Pin 2 to GND. <--");
  }

  Serial.println("\nEntering deep sleep now...");
  Serial.println("Note: RTC memory will be powered down automatically for max savings.");

  // Wait until all serial messages are sent before sleeping
  Serial.flush();

  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // This part is never reached.
}