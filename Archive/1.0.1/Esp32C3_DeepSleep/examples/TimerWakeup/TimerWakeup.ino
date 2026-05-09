/**
 * @file TimerWakeup.ino
 * @brief Updated example for waking up from deep sleep using the timer.
 *
 * This sketch demonstrates a simple timer wakeup. Because the timer is used,
 * the library will automatically keep RTC memory powered ON during sleep.
 * This is useful for sketches that need to preserve data in RTC memory,
 * like a boot counter.
 */

#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>

// The board will wake up every 10 seconds
#define SLEEP_TIME_SECONDS 10

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait for the serial monitor to connect
  Serial.println("\n--- Timer Wakeup Example (Updated) ---");

  // Check the reason for the wakeup
  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();

  // Differentiate between the first boot and a wakeup from the timer
  if (cause != ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Initial boot. Configuring timer wakeup...");
    // Configure the timer wakeup (time is in microseconds)
    Esp32C3_DeepSleep::beginTimerWakeup(SLEEP_TIME_SECONDS * 1000000ULL);
  } else {
    Serial.println("Woke up from timer!");
  }
  
  delay(1000); // Wait a moment for the user to read the message

  Serial.printf("\nEntering deep sleep for %d seconds...\n", SLEEP_TIME_SECONDS);
  Serial.println("Note: RTC memory will be kept ON automatically because a timer is used.");

  // Wait until all serial messages are sent before the chip goes to sleep
  Serial.flush();
  
  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // This code is never reached, as the ESP32-C3 always restarts in setup() after deep sleep.
}