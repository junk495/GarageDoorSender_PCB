/*
 * =======================================================
 * Esp32C3_DeepSleep.cpp (Verbesserte Version)
 * =======================================================
 * Implementierungsdatei für die Deep Sleep Bibliothek.
 */

#include "Esp32C3_DeepSleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

namespace Esp32C3_DeepSleep {

// --- Interne Zustandsvariablen ---
static uint64_t _wakeupPinMask = 0;
// NEU (Vorschlag 1): Weck-Level mit -1 initialisieren, um "nicht gesetzt" anzuzeigen.
static int _wakeupLevel = -1; 
static bool timerWakeupConfigured = false;
// NEU (Vorschlag 2): Merker für das manuelle Erzwingen des RTC-Speichers.
static bool rtcMemoryForcedOn = false;


void clearWakeupPins() {
  _wakeupPinMask = 0;
  // NEU (Vorschlag 1): Weck-Level ebenfalls zurücksetzen.
  _wakeupLevel = -1;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}

void addWakeupPin(uint8_t gpioPin, bool wakeOnHigh) {
  int desiredLevel = wakeOnHigh ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW;

  // NEU (Vorschlag 1): Prüfe auf widersprüchliche Konfigurationen.
  // Wenn bereits ein Level gesetzt ist und das neue anders ist, gib einen Fehler aus.
  if (_wakeupLevel != -1 && _wakeupLevel != desiredLevel) {
    #if DEBUG_MODE // Annahme, dass DEBUG_MODE im Hauptprojekt definiert ist
    Serial.println("DEEP SLEEP LIB ERROR: Cannot mix HIGH and LOW wakeup levels.");
    #endif
    return; // Breche die Funktion ab, um Fehlkonfiguration zu verhindern.
  }

  // Setze (oder bestätige) das Weck-Level für alle Pins.
  _wakeupLevel = desiredLevel;

  gpio_set_direction((gpio_num_t)gpioPin, GPIO_MODE_INPUT);
  gpio_set_pull_mode((gpio_num_t)gpioPin,
                      wakeOnHigh ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
  _wakeupPinMask |= (1ULL << gpioPin);
  
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_deep_sleep_enable_gpio_wakeup(_wakeupPinMask, (esp_deepsleep_gpio_wake_up_mode_t)_wakeupLevel);
}

void beginTimerWakeup(uint64_t timeUs) {
  esp_sleep_enable_timer_wakeup(timeUs);
  timerWakeupConfigured = true;
}

// NEU (Vorschlag 2): Implementierung der manuellen RTC-Speicher-Steuerung.
void keepRtcMemory(bool keepOn) {
    rtcMemoryForcedOn = keepOn;
}

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

[[noreturn]] void goDeepSleep() {
  // NEU (Vorschlag 2): Die Bedingung wurde erweitert. RTC bleibt an, WENN
  // der Timer konfiguriert ist ODER es manuell erzwungen wurde.
  if (timerWakeupConfigured || rtcMemoryForcedOn) {
    // Lasse den RTC-Speicher AN.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
  } else {
    // Schalte RTC-Speicher für maximale Ersparnis AUS.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  }
  
  esp_deep_sleep_start();
}

} // namespace Esp32C3_DeepSleep