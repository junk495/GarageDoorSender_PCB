#include "Esp32C3_DeepSleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

namespace Esp32C3_DeepSleep {

static uint64_t _wakeupPinMask = 0;
static int _wakeupLevel = ESP_GPIO_WAKEUP_GPIO_HIGH;
// NEU: Merker, ob der Timer als Weckquelle konfiguriert wurde.
static bool timerWakeupConfigured = false;

void clearWakeupPins() {
  _wakeupPinMask = 0;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}

void addWakeupPin(uint8_t gpioPin, bool wakeOnHigh) {
  gpio_set_direction((gpio_num_t)gpioPin, GPIO_MODE_INPUT);
  gpio_set_pull_mode((gpio_num_t)gpioPin,
                       wakeOnHigh ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
  _wakeupPinMask |= (1ULL << gpioPin);
  if ((_wakeupPinMask & (1ULL << gpioPin)) == (1ULL << gpioPin)) {
     _wakeupLevel = wakeOnHigh ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW;
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_deep_sleep_enable_gpio_wakeup(_wakeupPinMask, (esp_deepsleep_gpio_wake_up_mode_t)_wakeupLevel);
}

// GEÄNDERT: Die Funktion setzt jetzt den Merker
void beginTimerWakeup(uint64_t timeUs) {
  esp_sleep_enable_timer_wakeup(timeUs);
  timerWakeupConfigured = true;
}

// Die manuelle Konfigurationsfunktion wurde entfernt.

void holdGPIO(uint8_t gpioPin) {
  if (!rtc_gpio_is_valid_gpio((gpio_num_t)gpioPin)) {
    return;
  }
  gpio_hold_en((gpio_num_t)gpioPin);
}

void releaseAllHolds() {
  for (int pin = 0; pin < GPIO_PIN_COUNT; ++pin) {
    gpio_hold_dis((gpio_num_t)pin);
  }
}

esp_sleep_wakeup_cause_t wakeupCause() {
  return esp_sleep_get_wakeup_cause();
}

// GEÄNDERT: Die Funktion managt jetzt automatisch die Power-Domains
[[noreturn]] void goDeepSleep() {
  if (timerWakeupConfigured) {
    // Timer ist aktiv -> wir gehen davon aus, dass RTC-Speicher gebraucht wird (z.B. für Zähler).
    // Lasse den RTC-Speicher AN.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
  } else {
    // Kein Timer aktiv -> RTC-Speicher wird nicht gebraucht.
    // Schalte ihn für maximale Ersparnis AUS.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  }
  
  esp_deep_sleep_start();
}

} // namespace Esp32C3_DeepSleep