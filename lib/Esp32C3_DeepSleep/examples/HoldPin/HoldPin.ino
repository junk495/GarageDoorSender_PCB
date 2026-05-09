/**
 * @file HoldPin.ino
 * @brief Updated example for holding a GPIO pin's state during deep sleep.
 *
 * This sketch demonstrates the updated holdGPIO() function. Because a timer
 * is used to wake up, the library will automatically keep RTC memory
 * powered ON during sleep.
 *
 * Hardware Setup:
 * - An LED (with resistor) between GPIO 1 and GND.
 */

#include <Arduino.h>
#include <Esp32C3_DeepSleep.h>

#define HOLD_PIN 1
#define PIN_LEVEL HIGH
#define SLEEP_TIME_SECONDS 10

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--- Hold Pin Example (Updated) ---");

  pinMode(HOLD_PIN, OUTPUT);
  esp_sleep_wakeup_cause_t cause = Esp32C3_DeepSleep::wakeupCause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("Woke up from timer.");
      Esp32C3_DeepSleep::releaseAllHolds(); // Release hold to regain control
      Serial.println("GPIO holds released. Turning LED OFF.");
      digitalWrite(HOLD_PIN, !PIN_LEVEL);
      delay(3000);
  } else {
      Serial.println("Initial boot. Configuring GPIO hold and timer wakeup.");
      
      Esp32C3_DeepSleep::beginTimerWakeup(SLEEP_TIME_SECONDS * 1000000ULL);
      Serial.printf("Configured timer wakeup for %d seconds.\n", SLEEP_TIME_SECONDS);

      // UPDATED USAGE: First, set the pin to the desired state.
      Serial.printf("Turning LED ON (Pin %d).\n", HOLD_PIN);
      digitalWrite(HOLD_PIN, PIN_LEVEL);
      
      // Second, call holdGPIO() to latch that state for deep sleep.
      Esp32C3_DeepSleep::holdGPIO(HOLD_PIN);
      Serial.printf("Pin %d will be held HIGH during sleep.\n", HOLD_PIN);
  }

  Serial.println("\nEntering deep sleep now...");
  Serial.println("Note: RTC memory will be kept ON automatically because a timer is used.");

  // Wait until all serial messages are sent
  Serial.flush();

  Esp32C3_DeepSleep::goDeepSleep();
}

void loop() {
  // This part is never reached.
}